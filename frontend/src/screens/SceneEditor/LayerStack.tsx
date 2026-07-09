// Layer stack, TOP of the list = foreground (matches how a performer
// thinks about "background clip + generator riding on top"). The scene's
// layers array is bottom-to-top (compositing order), so this renders it
// reversed.

import { newId } from "../../api/client";
import type { AudioBand, BlendMode, Clip, EffectsManifest, ParamSpec, Scene } from "../../api/types";
import MappableControl, { makePreviewSender } from "../../components/MappableControl";
import { useShowStore } from "../../state/showStore";

export default function LayerStack({ scene, manifest, clips }: { scene: Scene; manifest: EffectsManifest; clips: Clip[] }) {
    const edit = useShowStore((s) => s.edit);

    const editLayer = (layerId: string, fn: (layer: Scene["layers"][number]) => void) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            const layer = s?.layers.find((l) => l.id === layerId);
            if (layer) fn(layer);
        });
    };

    const addLayer = () => {
        if (clips.length === 0) {
            alert("Upload a clip first (Clips tab).");
            return;
        }
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            s?.layers.push({
                id: newId("layer"),
                kind: "clip",
                source: clips[0].id,
                blendMode: "normal",
                opacity: 1.0,
                layerEffects: {},
                params: {},
            });
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
                const clip = clips.find((c) => c.id === layer.source);
                const index = scene.layers.findIndex((l) => l.id === layer.id);
                return (
                    <div key={layer.id} className="card" style={{ background: "var(--bg)", display: "flex", flexDirection: "column", gap: 8 }}>
                        <div className="row" style={{ justifyContent: "space-between", flexWrap: "wrap" }}>
                            <div className="row">
                                {clip?.thumbUrl ? (
                                    <img src={clip.thumbUrl} alt="" style={{ width: 64, height: 36, objectFit: "cover", borderRadius: 4 }} />
                                ) : (
                                    <div style={{ width: 64, height: 36, background: "var(--bg-input)", borderRadius: 4, display: "grid", placeItems: "center", fontSize: 11 }} className="dim">
                                        {layer.kind === "generator" ? "gen" : "?"}
                                    </div>
                                )}
                                <select
                                    value={layer.source}
                                    onChange={(e) => editLayer(layer.id, (l) => { l.source = e.target.value; })}
                                >
                                    {clips.map((c) => (
                                        <option key={c.id} value={c.id}>
                                            {c.name ?? c.path} {c.height ? `(${c.height}p)` : ""}
                                        </option>
                                    ))}
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
                        {Object.entries(manifest.layerEffects).map(([key, spec]) => (
                            <MappableControl
                                key={key}
                                label={spec.label}
                                spec={spec}
                                value={layer.layerEffects[key] ?? spec.default}
                                onChange={(v) => editLayer(layer.id, (l) => { l.layerEffects[key] = v; })}
                                midiMapping={midiMappingFor(layer.id, key)}
                                audioMapping={audioMappingFor(layer.id, key)}
                                onBindMidi={(trigger) => bindLayerMidi(layer.id, key, trigger, spec)}
                                onUnbindMidi={() => unbindLayerParam(layer.id, key, MIDI_TYPES)}
                                onBindAudio={(band, amount) => bindLayerAudio(layer.id, key, band, amount)}
                                onUnbindAudio={() => unbindLayerParam(layer.id, key, ["audioBand"])}
                                sendPreview={makePreviewSender(scene.id, `layer.${layer.id}.${key}`)}
                            />
                        ))}
                    </div>
                );
            })}
            {scene.layers.length === 0 && <div className="dim">No layers -- add a clip layer to give this scene a picture.</div>}
        </div>
    );
}
