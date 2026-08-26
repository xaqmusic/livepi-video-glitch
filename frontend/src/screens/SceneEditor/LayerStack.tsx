// Layer stack, TOP of the list = foreground (matches how a performer
// thinks about "background clip + generator riding on top"). The scene's
// layers array is bottom-to-top (compositing order), so this renders it
// reversed.
//
// A layer's source is ONE dropdown covering both clips and generators --
// picking from either group sets the layer's kind, no separate toggle.
// Controls file into collapsible sections (the `group` field on manifest
// specs); a closed section shows a hot dot when anything inside is
// non-default or bound, so a folded layer still reads at a glance.

import { useState, type ReactNode } from "react";

import { api, newId, pingpongKey } from "../../api/client";
import type { AudioBand, BlendMode, Clip, EffectsManifest, ParamSpec, Scene } from "../../api/types";
import MappableControl, { makePreviewSender } from "../../components/MappableControl";
import { useShowStore } from "../../state/showStore";

// Generator options carry this prefix in the source <select> so clip ids
// can never collide with generator names.
const GEN_PREFIX = "gen:";

// The 64x36 source thumbnail for a layer: a clip's own thumbnail, else a
// generator's static art from /thumbs/generators/<key>.png (shipped in the web
// build), falling back to a glyph if that PNG isn't present yet.
function SourceThumb({ clip, generatorKey }: { clip?: Clip; generatorKey?: string }) {
    const [failed, setFailed] = useState(false);
    const box = { width: 64, height: 36, borderRadius: 4 } as const;
    if (clip?.thumbUrl) {
        return <img src={clip.thumbUrl} alt="" style={{ ...box, objectFit: "cover" }} />;
    }
    if (generatorKey && !failed) {
        return (
            <img
                src={`/thumbs/generators/${generatorKey}.png`}
                alt=""
                style={{ ...box, objectFit: "cover" }}
                onError={() => setFailed(true)}
            />
        );
    }
    return (
        <div style={{ ...box, background: "var(--bg-input)", display: "grid", placeItems: "center", fontSize: 11 }} className="dim">
            {generatorKey ? "✳" : "?"}
        </div>
    );
}

function ParamGroup({ title, hot, onReset, children }: { title: string; hot: boolean; onReset?: () => void; children: ReactNode }) {
    return (
        <details className="param-group">
            <summary>
                {title}
                {hot && <span className="hot-dot" title="active" />}
                {onReset && (
                    <button
                        className="icon group-reset"
                        title={`Reset every ${title} param to its default (no effect). MIDI and audio bindings are kept.`}
                        // A <summary> toggles on click and treats Enter/Space as
                        // activation, so BOTH have to be stopped or resetting the
                        // group would also collapse it.
                        onClick={(e) => { e.preventDefault(); e.stopPropagation(); onReset(); }}
                        onKeyDown={(e) => e.stopPropagation()}
                    >
                        reset
                    </button>
                )}
            </summary>
            <div className="param-group-body">{children}</div>
        </details>
    );
}

// Ping-pong reverse plays a baked "boomerang" (forward + pre-reversed segment
// as one forward-looping file) -- the Pi's decoder can't run backwards, so
// the reverse has to be pre-rendered per clip + trim. This row bakes it and
// reflects whether the file the renderer looks for already exists.
function PingpongPrep({ layer, clip, onBaked }: {
    layer: Scene["layers"][number];
    clip: Clip;
    onBaked: () => Promise<void> | void;
}) {
    const [baking, setBaking] = useState(false);
    const on = (layer.layerEffects["video.pingpong"] ?? 0) > 0.5;
    if (!on) return null;

    const start = layer.layerEffects["video.start"] ?? 0;
    const end = layer.layerEffects["video.end"] ?? 1;
    const [ks, ke] = [pingpongKey(start), pingpongKey(end)];
    const ready = (clip.pingpong ?? []).some(([a, b]) => a === ks && b === ke);

    const bake = async () => {
        setBaking(true);
        try {
            const { jobId } = await api.bakePingpong(clip.id, start, end);
            for (;;) {
                await new Promise((r) => setTimeout(r, 1500));
                const st = await api.jobStatus(jobId);
                if (st.state === "done") break;
                if (st.state === "error") throw new Error(st.error || "bake failed");
            }
            await onBaked();
        } catch (e) {
            alert(String(e));
        } finally {
            setBaking(false);
        }
    };

    return (
        <>
            <div className="fx-divider" />
            <div className="row" style={{ justifyContent: "space-between", fontSize: 12, gap: 8 }}>
                {ready ? (
                    <span className="dim">↔ reverse baked</span>
                ) : (
                    <span className="warn" style={{ padding: 0, background: "none", border: "none" }}>
                        reverse not baked — plays forward only
                    </span>
                )}
                <button
                    disabled={baking}
                    title="Pre-render the reverse segment so ping-pong loops flawlessly on the Pi (bake again after changing start/end)"
                    onClick={() => void bake()}
                >
                    {baking ? "baking…" : ready ? "Re-bake" : "Bake reverse"}
                </button>
            </div>
        </>
    );
}

export default function LayerStack({ scene, manifest, clips, onClipsChanged }: {
    scene: Scene;
    manifest: EffectsManifest;
    clips: Clip[];
    onClipsChanged: () => Promise<void> | void;
}) {
    const edit = useShowStore((s) => s.edit);

    const editLayer = (layerId: string, fn: (layer: Scene["layers"][number]) => void) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            const layer = s?.layers.find((l) => l.id === layerId);
            if (layer) fn(layer);
        });
    };

    const generatorNames = Object.keys(manifest.generators);
    // Every generator param key anywhere, for cleaning up when a layer's
    // source switches (old generator's params + their mappings go stale).
    const allGeneratorParamKeys = new Set(
        generatorNames.flatMap((g) => Object.keys(manifest.generators[g].params)),
    );

    const addLayer = () => {
        if (clips.length === 0 && generatorNames.length === 0) {
            alert("Upload a clip first (Clips tab).");
            return;
        }
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            s?.layers.push({
                id: newId("layer"),
                kind: clips.length > 0 ? "clip" : "generator",
                source: clips.length > 0 ? clips[0].id : generatorNames[0],
                blendMode: "normal",
                opacity: 1.0,
                layerEffects: {},
                params: {},
            });
        });
    };

    const setLayerSource = (layerId: string, value: string) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            const layer = s?.layers.find((l) => l.id === layerId);
            if (!s || !layer) return;
            const toGenerator = value.startsWith(GEN_PREFIX);
            layer.kind = toGenerator ? "generator" : "clip";
            layer.source = toGenerator ? value.slice(GEN_PREFIX.length) : value;
            // Drop params (and mapping targets) that belonged to a previous
            // generator -- the backend sanitizer would strip them anyway,
            // but doing it here keeps the draft clean and warning-free.
            const keep = toGenerator ? new Set(Object.keys(manifest.generators[layer.source]?.params ?? {})) : new Set<string>();
            for (const key of Object.keys(layer.params)) {
                if (!keep.has(key)) delete layer.params[key];
            }
            s.mappings = s.mappings
                .map((m) => ({
                    ...m,
                    targets: m.targets.filter(
                        (t) => !(t.layerId === layerId && allGeneratorParamKeys.has(t.param) && !keep.has(t.param)),
                    ),
                }))
                .filter((m) => m.targets.length > 0);
        });
    };

    const clipCount = scene.layers.filter((l) => l.kind === "clip").length;
    const overBudget = clipCount > manifest.layerBudget.maxClipLayers;

    const MIDI_TYPES = ["cc", "note"];

    const targetsLayerParam = (m: Scene["mappings"][number], layerId: string, param: string) =>
        m.targets.some((t) => t.layerId === layerId && t.param === param);

    // The binding's amount is carried in the target SPAN, not a separate field:
    // amount = (max - min) / spec span. An older binding written as
    // [spec.min, spec.max] therefore reads back as exactly 1.0, so nothing
    // needs migrating.
    const midiMappingFor = (layerId: string, param: string) => {
        const m = scene.mappings.find((x) => MIDI_TYPES.includes(x.trigger.type) && targetsLayerParam(x, layerId, param));
        if (!m) return null;
        const target = m.targets.find((t) => t.layerId === layerId && t.param === param)!;
        const spec = manifest.layerEffects[param];
        const span = spec ? spec.max - spec.min : 1;
        return { trigger: m.trigger, amount: span ? (target.max - target.min) / span : 1 };
    };

    const audioMappingFor = (layerId: string, param: string) => {
        const m = scene.mappings.find((x) => x.trigger.type === "audioBand" && targetsLayerParam(x, layerId, param));
        if (!m) return null;
        const target = m.targets.find((t) => t.layerId === layerId && t.param === param)!;
        return { band: m.trigger.band as AudioBand, amount: target.max };
    };

    const removeLayerBindings = (draftScene: Scene, layerId: string, param: string, types: string[]) => {
        draftScene.mappings = draftScene.mappings
            .map((m) =>
                types.includes(m.trigger.type)
                    ? { ...m, targets: m.targets.filter((t) => !(t.layerId === layerId && t.param === param)) }
                    : m,
            )
            .filter((m) => m.targets.length > 0);
    };

    const bindLayerMidi = (
        layerId: string,
        param: string,
        trigger: Scene["mappings"][number]["trigger"],
        spec?: ParamSpec,
        amount = 1,
    ) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            removeLayerBindings(s, layerId, param, MIDI_TYPES);
            // Same shape as an audio binding: min 0, max = the contribution the
            // knob adds at full travel. amount 1 reproduces the old full-span sweep.
            const span = (spec?.max ?? 1) - (spec?.min ?? 0);
            s.mappings.push({ trigger, targets: [{ layerId, param, min: 0, max: amount * span }] });
        });
    };

    const bindLayerAudio = (layerId: string, param: string, band: AudioBand, amount: number) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            removeLayerBindings(s, layerId, param, ["audioBand"]);
            s.mappings.push({ trigger: { type: "audioBand", band }, targets: [{ layerId, param, min: 0, max: amount }] });
        });
    };

    const unbindLayerParam = (layerId: string, param: string, types: string[]) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (s) removeLayerBindings(s, layerId, param, types);
        });
    };

    // One MappableControl wired to a layer param, whichever map it lives in.
    const renderControl = (
        layer: Scene["layers"][number],
        key: string,
        spec: ParamSpec,
        store: "layerEffects" | "params",
    ) => (
        <MappableControl
            key={key}
            label={spec.label}
            spec={spec}
            value={layer[store][key] ?? spec.default}
            onChange={(v) => editLayer(layer.id, (l) => { l[store][key] = v; })}
            midiMapping={midiMappingFor(layer.id, key)}
            audioMapping={audioMappingFor(layer.id, key)}
            onBindMidi={(trigger, amount) => bindLayerMidi(layer.id, key, trigger, spec, amount)}
            onUnbindMidi={() => unbindLayerParam(layer.id, key, MIDI_TYPES)}
            onBindAudio={(band, amount) => bindLayerAudio(layer.id, key, band, amount)}
            onUnbindAudio={() => unbindLayerParam(layer.id, key, ["audioBand"])}
            sendPreview={makePreviewSender(scene.id, `layer.${layer.id}.${key}`)}
        />
    );

    // Reset every param in one group back to its manifest default -- which IS
    // the no-effect state for all of them (amounts default to 0; neutral params
    // like transform.scale and color.brightness default to their identity
    // value). Deliberately does NOT touch mappings: a group full of sliders is
    // hard to walk back by hand, but a MIDI/audio binding took effort to set up
    // and losing it to a "reset" would be a nasty surprise.
    //
    // Each param is also pushed to the renderer so the change is instant rather
    // than waiting on the debounced auto-save.
    const resetGroup = (layerId: string, entries: [string, ParamSpec][], store: "layerEffects" | "params") => {
        editLayer(layerId, (l) => {
            for (const [key, spec] of entries) l[store][key] = spec.default;
        });
        for (const [key, spec] of entries) {
            makePreviewSender(scene.id, `layer.${layerId}.${key}`)(spec.default);
        }
    };

    const paramHot = (layer: Scene["layers"][number], key: string, spec: ParamSpec, store: "layerEffects" | "params") =>
        (layer[store][key] ?? spec.default) !== spec.default ||
        midiMappingFor(layer.id, key) !== null ||
        audioMappingFor(layer.id, key) !== null;

    // layerEffects bucketed by their manifest `group`, preserving both the
    // group order and in-group order of the manifest.
    const effectGroups: { title: string; entries: [string, ParamSpec][] }[] = [];
    for (const [key, spec] of Object.entries(manifest.layerEffects)) {
        const title = spec.group ?? "Effects";
        let bucket = effectGroups.find((g) => g.title === title);
        if (!bucket) {
            bucket = { title, entries: [] };
            effectGroups.push(bucket);
        }
        bucket.entries.push([key, spec]);
    }

    // Reversed for display: index 0 shown last (bottom).
    const displayLayers = [...scene.layers].reverse();

    return (
        <div className="card" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <div className="row" style={{ justifyContent: "space-between" }}>
                <h3 style={{ margin: 0 }}>Layers <span className="dim" style={{ fontWeight: 400 }}>(top = foreground)</span></h3>
                <button onClick={addLayer}>Add layer</button>
            </div>
            {overBudget && (
                <div className="warn">
                    ⚠ {clipCount} clip layers exceeds the Pi's measured decode budget ({manifest.layerBudget.maxClipLayers}).
                </div>
            )}
            {displayLayers.map((layer) => {
                const clip = layer.kind === "clip" ? clips.find((c) => c.id === layer.source) : undefined;
                const generator = layer.kind === "generator" ? manifest.generators[layer.source] : undefined;
                const index = scene.layers.findIndex((l) => l.id === layer.id);
                const sourceValue = layer.kind === "generator" ? GEN_PREFIX + layer.source : layer.source;
                return (
                    <div key={layer.id} className="card" style={{ background: "var(--bg)", display: "flex", flexDirection: "column", gap: 8 }}>
                        <div className="row" style={{ justifyContent: "space-between", flexWrap: "wrap" }}>
                            <div className="row">
                                <SourceThumb clip={clip} generatorKey={layer.kind === "generator" ? layer.source : undefined} />
                                <select value={sourceValue} onChange={(e) => setLayerSource(layer.id, e.target.value)}>
                                    <optgroup label="Clips">
                                        {clips.filter((c) => c.kind !== "image").map((c) => (
                                            <option key={c.id} value={c.id}>
                                                {c.name ?? c.path} {c.height ? `(${c.height}p)` : ""}
                                            </option>
                                        ))}
                                    </optgroup>
                                    {clips.some((c) => c.kind === "image") && (
                                        <optgroup label="Images">
                                            {clips.filter((c) => c.kind === "image").map((c) => (
                                                <option key={c.id} value={c.id}>{c.name ?? c.path}</option>
                                            ))}
                                        </optgroup>
                                    )}
                                    <optgroup label="Generators">
                                        {generatorNames.filter((g) => !manifest.generators[g].trigger).map((g) => (
                                            <option key={g} value={GEN_PREFIX + g}>{manifest.generators[g].label}</option>
                                        ))}
                                    </optgroup>
                                    <optgroup label="Note Generators">
                                        {generatorNames.filter((g) => manifest.generators[g].trigger === "notes").map((g) => (
                                            <option key={g} value={GEN_PREFIX + g}>{manifest.generators[g].label}</option>
                                        ))}
                                    </optgroup>
                                </select>
                                <select
                                    value={layer.blendMode}
                                    onChange={(e) => editLayer(layer.id, (l) => { l.blendMode = e.target.value as BlendMode; })}
                                >
                                    {manifest.blendModes.map((mode) => (
                                        <option key={mode} value={mode}>{mode}</option>
                                    ))}
                                </select>
                            </div>
                            <div className="row">
                                <button className="icon" disabled={index === scene.layers.length - 1}
                                    title="Bring forward"
                                    onClick={() => edit((draft) => {
                                        const s = draft.scenes.find((x) => x.id === scene.id);
                                        if (s && index < s.layers.length - 1)
                                            [s.layers[index], s.layers[index + 1]] = [s.layers[index + 1], s.layers[index]];
                                    })}>▲</button>
                                <button className="icon" disabled={index === 0}
                                    title="Send back"
                                    onClick={() => edit((draft) => {
                                        const s = draft.scenes.find((x) => x.id === scene.id);
                                        if (s && index > 0)
                                            [s.layers[index], s.layers[index - 1]] = [s.layers[index - 1], s.layers[index]];
                                    })}>▼</button>
                                <button className="danger icon"
                                    onClick={() => edit((draft) => {
                                        const s = draft.scenes.find((x) => x.id === scene.id);
                                        if (!s) return;
                                        s.layers = s.layers.filter((l) => l.id !== layer.id);
                                        s.mappings = s.mappings
                                            .map((m) => ({ ...m, targets: m.targets.filter((t) => t.layerId !== layer.id) }))
                                            .filter((m) => m.targets.length > 0);
                                    })}>✕</button>
                            </div>
                        </div>
                        <MappableControl
                            label="Opacity"
                            spec={{ label: "Opacity", type: "float", min: 0, max: 1, default: 1 }}
                            value={layer.opacity}
                            onChange={(v) => editLayer(layer.id, (l) => { l.opacity = v; })}
                            midiMapping={midiMappingFor(layer.id, "opacity")}
                            audioMapping={audioMappingFor(layer.id, "opacity")}
                            onBindMidi={(trigger) => bindLayerMidi(layer.id, "opacity", trigger)}
                            onUnbindMidi={() => unbindLayerParam(layer.id, "opacity", MIDI_TYPES)}
                            onBindAudio={(band, amount) => bindLayerAudio(layer.id, "opacity", band, amount)}
                            onUnbindAudio={() => unbindLayerParam(layer.id, "opacity", ["audioBand"])}
                            sendPreview={makePreviewSender(scene.id, `layer.${layer.id}.opacity`)}
                        />
                        {generator && (
                            <ParamGroup
                                title={generator.label}
                                hot={Object.entries(generator.params).some(([k, s]) => paramHot(layer, k, s, "params"))}
                                onReset={() => resetGroup(layer.id, Object.entries(generator.params), "params")}
                            >
                                {Object.entries(generator.params).map(([key, spec]) => renderControl(layer, key, spec, "params"))}
                            </ParamGroup>
                        )}
                        {effectGroups
                            // Transform/Playback drive the CLIP inside the
                            // frame; generators paint the full frame and
                            // have no player -- hide rather than lie.
                            .filter((g) => !(layer.kind === "generator" && (g.title === "Transform" || g.title === "Playback")))
                            .map((g) => (
                                <ParamGroup
                                    key={g.title}
                                    title={g.title}
                                    hot={g.entries.some(([k, s]) => paramHot(layer, k, s, "layerEffects"))}
                                    onReset={() => resetGroup(layer.id, g.entries, "layerEffects")}
                                >
                                    {g.entries.map(([key, spec], i) => {
                                        // Thin divider whenever the effect family
                                        // (key prefix) changes: Fracture's eight
                                        // sliders read as one block, apart from
                                        // Tunnel's, and so on.
                                        const prefix = key.split(".")[0];
                                        const prevPrefix = i > 0 ? g.entries[i - 1][0].split(".")[0] : prefix;
                                        return (
                                            <div key={key} style={{ display: "contents" }}>
                                                {prefix !== prevPrefix && <div className="fx-divider" />}
                                                {renderControl(layer, key, spec, "layerEffects")}
                                            </div>
                                        );
                                    })}
                                    {g.title === "Playback" && layer.kind === "clip" && clip && (
                                        <PingpongPrep layer={layer} clip={clip} onBaked={onClipsChanged} />
                                    )}
                                </ParamGroup>
                            ))}
                    </div>
                );
            })}
            {scene.layers.length === 0 && <div className="dim">No layers -- add a clip or generator to give this scene a picture.</div>}
        </div>
    );
}
