// Live mode (docs/videosynth-frontend.md): phone-first, mid-set. Current
// scene from telemetry, Next mirroring the physical button exactly, Back
// computed client-side, and one big slider per CC mapping in the current
// scene -- sending cc commands makes the phone indistinguishable from the
// physical knob (same absolute semantics, same fan-out, last-writer-wins;
// audio-band modulation keeps riding on top). No bidirectional sync:
// absolute-send-on-touch, like hardware.

import { useEffect, useRef, useState } from "react";

import { api } from "../api/client";
import type { Show } from "../api/types";
import { useTelemetry } from "../state/telemetryStore";

export default function LiveMode() {
    const telemetry = useTelemetry();
    const [show, setShow] = useState<Show | null>(null);
    const [offline, setOffline] = useState(false);

    useEffect(() => {
        api.listShows()
            .then((r) => (r.active ? api.getShow(r.active) : null))
            .then(setShow)
            .catch(() => setShow(null));
    }, []);

    const send = (cmd: Record<string, unknown>) =>
        api.command(cmd).then(() => setOffline(false)).catch(() => setOffline(true));

    const sceneId = telemetry?.currentSceneId;
    const scene = show?.scenes.find((s) => s.id === sceneId);
    const sceneIndex = show && sceneId ? show.scenes.findIndex((s) => s.id === sceneId) : -1;
    const prevScene = show && sceneIndex > 0 ? show.scenes[sceneIndex - 1] : null;

    const ccMappings = scene?.mappings.filter((m) => m.trigger.type === "cc") ?? [];

    return (
        <div style={{ display: "flex", flexDirection: "column", minHeight: "100dvh", padding: 16, gap: 16 }}>
            <div style={{ textAlign: "center" }}>
                <div className="dim" style={{ fontSize: 13 }}>
                    {offline ? <span className="error">renderer offline</span> : telemetry ? `${telemetry.fps.toFixed(0)} fps` : "connecting…"}
                </div>
                <h1 style={{ fontSize: 28, margin: "4px 0" }}>
                    {telemetry?.currentSceneName ?? "—"}
                </h1>
                {sceneIndex >= 0 && show && (
                    <div className="dim">
                        {sceneIndex + 1} / {show.scenes.length}
                    </div>
                )}
            </div>

            <div style={{ display: "flex", gap: 12 }}>
                <button
                    style={{ flex: 1, padding: "22px 0", fontSize: 18 }}
                    disabled={!prevScene}
                    onClick={() => prevScene && void send({ type: "goto", sceneId: prevScene.id })}
                >
                    ◀ Back
                </button>
                <button
                    className="primary"
                    style={{ flex: 2, padding: "22px 0", fontSize: 18 }}
                    onClick={() => void send({ type: "click" })}
                >
                    Next ▶
                </button>
            </div>

            <div style={{ display: "flex", flexDirection: "column", gap: 14, flex: 1 }}>
                {ccMappings.map((mapping, i) => (
                    <BigSlider
                        key={`${sceneId}-${i}`}
                        label={mapping.targets
                            .map((t) => {
                                if (!t.layerId) return t.param.replace(/^postEffects\./, "");
                                const li = scene!.layers.findIndex((l) => l.id === t.layerId);
                                return `Layer ${li + 1} ${t.param}`;
                            })
                            .join(" + ")}
                        ccNumber={mapping.trigger.number!}
                        initial={telemetry && telemetry.lastCc.cc === mapping.trigger.number ? telemetry.lastCc.value : 0.5}
                        onSend={(value) => void send({ type: "cc", number: mapping.trigger.number, value })}
                    />
                ))}
                {scene && ccMappings.length === 0 && (
                    <div className="dim" style={{ textAlign: "center", marginTop: 24 }}>
                        No CC mappings in this scene -- map some in the editor and they'll appear here.
                    </div>
                )}
            </div>
        </div>
    );
}

function BigSlider({ label, ccNumber, initial, onSend }: { label: string; ccNumber: number; initial: number; onSend: (v: number) => void }) {
    const [value, setValue] = useState(initial);
    const lastSent = useRef(0);
    const pending = useRef<number | null>(null);
    const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

    // Throttle to ~30Hz, latest wins.
    const send = (v: number) => {
        const now = Date.now();
        if (now - lastSent.current > 33) {
            lastSent.current = now;
            onSend(v);
        } else {
            pending.current = v;
            if (!timer.current) {
                timer.current = setTimeout(() => {
                    timer.current = null;
                    if (pending.current !== null) onSend(pending.current);
                    pending.current = null;
                }, 40);
            }
        }
    };

    return (
        <div className="card" style={{ padding: 16 }}>
            <div className="row" style={{ justifyContent: "space-between", marginBottom: 8 }}>
                <strong>{label}</strong>
                <span className="dim">CC {ccNumber}</span>
            </div>
            <input
                type="range"
                min={0}
                max={1}
                step={0.005}
                value={value}
                style={{ height: 36 }}
                onChange={(e) => {
                    const v = parseFloat(e.target.value);
                    setValue(v);
                    send(v);
                }}
            />
        </div>
    );
}
