// Audit view: every binding in this scene, resolved to human names (never
// raw ids), editable ranges, plus the live heaviness indicator fed by the
// renderer's frame-time telemetry (flag above ~28ms -- the performer's
// 30fps floor, docs/architecture.md).

import type { EffectsManifest, ParamSpec, Scene } from "../../api/types";
import { useShowStore } from "../../state/showStore";
import { useTelemetry } from "../../state/telemetryStore";

const HEAVY_FRAME_MS = 28;

export default function MappingsTab({ scene, manifest }: { scene: Scene; manifest: EffectsManifest }) {
    const edit = useShowStore((s) => s.edit);
    const telemetry = useTelemetry();

    const describeTarget = (layerId: string | null | undefined, param: string) => {
        if (!layerId) {
            const bare = param.replace(/^postEffects\./, "");
            return manifest.postEffects[bare]?.label ?? bare;
        }
        const index = scene.layers.findIndex((l) => l.id === layerId);
        return `Layer ${index + 1} ${param}`;
    };

    // Resolve a mapping target back to its ParamSpec so the range inputs below
    // can use the param's REAL bounds. Hardcoding 0..1 there silently made every
    // signed param un-editable from this tab: transform.x/y and rotozoom.speed/
    // zoom are -1..1, transform.rotation is -180..180, transform.scale goes to
    // 3.0 -- so a negative or >1 bound couldn't be entered or stepped to.
    // Mirrors describeTarget's layer-vs-post split, plus generator params, which
    // live on the layer's own generator rather than in layerEffects.
    const specFor = (layerId: string | null | undefined, param: string): ParamSpec | undefined => {
        if (!layerId) return manifest.postEffects[param.replace(/^postEffects\./, "")];
        const layer = scene.layers.find((l) => l.id === layerId);
        if (layer?.kind === "generator") {
            const genSpec = manifest.generators[layer.source]?.params[param];
            if (genSpec) return genSpec;
        }
        return manifest.layerEffects[param];
    };

    const isCurrentOnBox = telemetry?.currentSceneId === scene.id;
    const heavy = isCurrentOnBox && telemetry && telemetry.frameTimeMs > HEAVY_FRAME_MS;

    return (
        <div className="card" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <div className="row" style={{ justifyContent: "space-between" }}>
                <h3 style={{ margin: 0 }}>Mappings</h3>
                {isCurrentOnBox && telemetry && (
                    <span className={heavy ? "warn" : "dim"} style={{ fontSize: 12 }}>
                        {heavy ? "⚠ heavy: " : "on the box now: "}
                        {telemetry.fps.toFixed(0)} fps / {telemetry.frameTimeMs.toFixed(1)} ms
                    </span>
                )}
            </div>
            {scene.mappings.map((mapping, mi) => (
                <div key={mi} className="card" style={{ background: "var(--bg)", display: "flex", flexDirection: "column", gap: 6 }}>
                    <div className="row" style={{ justifyContent: "space-between" }}>
                        <strong>
                            {mapping.trigger.type === "cc"
                                ? `CC ${mapping.trigger.number}`
                                : mapping.trigger.type === "note"
                                  ? `Note ${mapping.trigger.number}`
                                  : `Audio ${mapping.trigger.band} band`}
                        </strong>
                        <button
                            className="danger icon"
                            onClick={() => edit((draft) => {
                                const s = draft.scenes.find((x) => x.id === scene.id);
                                s?.mappings.splice(mi, 1);
                            })}
                        >
                            ✕
                        </button>
                    </div>
                    {mapping.targets.map((target, ti) => {
                        const spec = specFor(target.layerId, target.param);
                        const lo = spec?.min ?? 0;
                        const hi = spec?.max ?? 1;
                        // step="any": with a computed numeric step the browser
                        // rejects any typed value off the step grid, which would
                        // trade one un-enterable-value bug for another.
                        return (
                            <div key={ti} className="row" style={{ fontSize: 13 }}>
                                <span style={{ flex: 1 }}>→ {describeTarget(target.layerId, target.param)}</span>
                                <span className="dim">range</span>
                                <input
                                    type="number" step="any" min={lo} max={hi} value={target.min}
                                    style={{ width: 64 }}
                                    onChange={(e) => edit((draft) => {
                                        const t = draft.scenes.find((x) => x.id === scene.id)?.mappings[mi]?.targets[ti];
                                        const v = parseFloat(e.target.value);
                                        // Fall back to the spec bound, not 0/1: on a -1..1 param a
                                        // mid-edit empty field would otherwise snap to 0.
                                        if (t) t.min = Number.isFinite(v) ? v : lo;
                                    })}
                                />
                                <span className="dim">to</span>
                                <input
                                    type="number" step="any" min={lo} max={hi} value={target.max}
                                    style={{ width: 64 }}
                                    onChange={(e) => edit((draft) => {
                                        const t = draft.scenes.find((x) => x.id === scene.id)?.mappings[mi]?.targets[ti];
                                        const v = parseFloat(e.target.value);
                                        if (t) t.max = Number.isFinite(v) ? v : hi;
                                    })}
                                />
                            </div>
                        );
                    })}
                </div>
            ))}
            {scene.mappings.length === 0 && (
                <div className="dim">
                    Nothing mapped -- click the MIDI button next to any parameter and turn a knob.
                </div>
            )}
        </div>
    );
}
