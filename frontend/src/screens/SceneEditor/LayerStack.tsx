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

import type { ReactNode } from "react";

import { newId } from "../../api/client";
import type { AudioBand, BlendMode, Clip, EffectsManifest, ParamSpec, Scene } from "../../api/types";
import MappableControl, { makePreviewSender } from "../../components/MappableControl";
import { useShowStore } from "../../state/showStore";

// Generator options carry this prefix in the source <select> so clip ids
// can never collide with generator names.
const GEN_PREFIX = "gen:";

function ParamGroup({ title, hot, children }: { title: string; hot: boolean; children: ReactNode }) {
    return (
        <details className="param-group">
            <summary>
                {title}
                {hot && <span className="hot-dot" title="active" />}
            </summary>
            <div className="param-group-body">{children}</div>
        </details>
    );
}

export default function LayerStack({ scene, manifest, clips }: { scene: Scene; manifest: EffectsManifest; clips: Clip[] }) {
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

    const midiMappingFor = (layerId: string, param: string) =>
        scene.mappings.find((m) => MIDI_TYPES.includes(m.trigger.type) && targetsLayerParam(m, layerId, param)) ?? null;

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

    const bindLayerMidi = (layerId: string, param: string, trigger: Scene["mappings"][number]["trigger"], spec?: ParamSpec) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            removeLayerBindings(s, layerId, param, MIDI_TYPES);
            s.mappings.push({ trigger, targets: [{ layerId, param, min: spec?.min ?? 0, max: spec?.max ?? 1 }] });
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
            onBindMidi={(trigger) => bindLayerMidi(layer.id, key, trigger, spec)}
            onUnbindMidi={() => unbindLayerParam(layer.id, key, MIDI_TYPES)}
            onBindAudio={(band, amount) => bindLayerAudio(layer.id, key, band, amount)}
            onUnbindAudio={() => unbindLayerParam(layer.id, key, ["audioBand"])}
            sendPreview={makePreviewSender(scene.id, `layer.${layer.id}.${key}`)}
        />
    );

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
                                {clip?.thumbUrl ? (
                                    <img src={clip.thumbUrl} alt="" style={{ width: 64, height: 36, objectFit: "cover", borderRadius: 4 }} />
                                ) : (
                                    <div style={{ width: 64, height: 36, background: "var(--bg-input)", borderRadius: 4, display: "grid", placeItems: "center", fontSize: 11 }} className="dim">
                                        {layer.kind === "generator" ? "✳" : "?"}
                                    </div>
                                )}
                                <select value={sourceValue} onChange={(e) => setLayerSource(layer.id, e.target.value)}>
                                    <optgroup label="Clips">
                                        {clips.map((c) => (
                                            <option key={c.id} value={c.id}>
                                                {c.name ?? c.path} {c.height ? `(${c.height}p)` : ""}
                                            </option>
                                        ))}
                                    </optgroup>
                                    <optgroup label="Generators">
                                        {generatorNames.map((g) => (
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
                            >
                                {Object.entries(generator.params).map(([key, spec]) => renderControl(layer, key, spec, "params"))}
                            </ParamGroup>
                        )}
                        {effectGroups
                            // Transform positions the CLIP inside the frame;
                            // generators paint the full frame, so it has no
                            // effect on them -- hide rather than lie.
                            .filter((g) => !(layer.kind === "generator" && g.title === "Transform"))
                            .map((g) => (
                                <ParamGroup
                                    key={g.title}
                                    title={g.title}
                                    hot={g.entries.some(([k, s]) => paramHot(layer, k, s, "layerEffects"))}
                                >
                                    {g.entries.map(([key, spec]) => renderControl(layer, key, spec, "layerEffects"))}
                                </ParamGroup>
                            ))}
                    </div>
                );
            })}
            {scene.layers.length === 0 && <div className="dim">No layers -- add a clip or generator to give this scene a picture.</div>}
        </div>
    );
}
