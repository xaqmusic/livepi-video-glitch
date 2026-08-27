// Bind a MIDI note/pad or a Program Change to jump straight to THIS scene.
//
// Per-scene and saved in the show, because a scene id only means something
// inside its own show -- a device-global table would point at nothing the
// moment a different show loaded. The device-global bindings in Settings stay
// what they are: relative moves (next scene / first scene), which work across
// shows precisely because they name no scene.
//
// Learn works like the param bindings: arm, then play the key or send the
// program change, and the first thing that arrives wins. It reads the same
// telemetry feed those use, so nothing new is plumbed.
import { useEffect, useRef, useState } from "react";

import type { Scene } from "../../api/types";
import { useShowStore } from "../../state/showStore";
import { useTelemetry, useTelemetryStore } from "../../state/telemetryStore";

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/** 60 -> "C4", matching what most DAWs and controllers print on their pads. */
function noteName(n: number): string {
    return `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;
}

export function describeSceneSelect(sel: Scene["sceneSelect"]): string {
    if (!sel) return "";
    return sel.type === "note" ? `${noteName(sel.number)} (${sel.number})` : `PC ${sel.number}`;
}

export default function SceneMidiTrigger({ scene }: { scene: Scene }) {
    const edit = useShowStore((s) => s.edit);
    const [armed, setArmed] = useState(false);
    const [menuOpen, setMenuOpen] = useState(false);
    const armedAt = useRef(0);
    const rootRef = useRef<HTMLDivElement | null>(null);
    useTelemetry();                       // keep the shared WS alive while mounted
    const connected = useTelemetryStore((s) => s.connected);

    const bind = (sel: Scene["sceneSelect"]) => {
        edit((draft) => {
            const s = draft.scenes.find((x) => x.id === scene.id);
            if (!s) return;
            s.sceneSelect = sel;
            // A binding must be unique across the show: two scenes on the same
            // note would make the second unreachable, since the renderer takes
            // the first match. Clear it elsewhere rather than silently shadowing.
            if (sel) {
                for (const other of draft.scenes) {
                    if (other.id !== scene.id && other.sceneSelect
                        && other.sceneSelect.type === sel.type
                        && other.sceneSelect.number === sel.number) {
                        other.sceneSelect = null;
                    }
                }
            }
        });
    };

    // Imperative store subscription, not a render-cycle effect -- same reason
    // MappableControl does it: the telemetry frame changes constantly and a
    // dependency on it would re-subscribe on every frame.
    useEffect(() => {
        if (!armed) return;
        let done = false;
        const tryBind = (latest: ReturnType<typeof useTelemetryStore.getState>["latest"]) => {
            const lc = latest?.lastControl;
            // Strictly newer than the arming moment: lastControl persists, so
            // without this, arming Learn on a second scene instantly re-binds
            // the note used for the first one and steals it away.
            if (done || !lc || lc.kind === "none" || lc.ts <= armedAt.current) return;
            // Only a NOTE arrives on this feed today; a CC would be the wrong
            // kind of trigger for scene selection, so ignore it rather than
            // binding something that cannot work.
            if (lc.kind !== "note") return;
            done = true;
            setArmed(false);
            bind({ type: "note", number: lc.number });
        };
        tryBind(useTelemetryStore.getState().latest);
        const unsub = useTelemetryStore.subscribe((s) => tryBind(s.latest));
        const timeout = setTimeout(() => setArmed(false), 15000);   // never listen forever
        return () => { unsub(); clearTimeout(timeout); };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [armed]);

    useEffect(() => {
        if (!menuOpen) return;
        const onDown = (e: Event) => {
            if (rootRef.current?.contains(e.target as Node)) return;
            setMenuOpen(false);
        };
        document.addEventListener("mousedown", onDown);
        return () => document.removeEventListener("mousedown", onDown);
    }, [menuOpen]);

    const label = scene.sceneSelect ? describeSceneSelect(scene.sceneSelect) : "MIDI";

    return (
        <div style={{ position: "relative" }} ref={rootRef}>
            <button
                className="icon"
                title="Jump straight to this scene from a MIDI key/pad or a Program Change"
                style={{
                    borderColor: armed ? "var(--warn)" : scene.sceneSelect ? "var(--accent)" : undefined,
                    color: armed ? "var(--warn)" : scene.sceneSelect ? "var(--accent)" : "var(--text-dim)",
                    minWidth: 68,
                }}
                onClick={() => (armed ? setArmed(false) : setMenuOpen(!menuOpen))}
            >
                {armed ? (connected ? "listen…" : "no WS!") : label}
            </button>
            {menuOpen && (
                <div
                    className="card"
                    style={{ position: "absolute", right: 0, top: "110%", zIndex: 30, width: 260,
                             display: "flex", flexDirection: "column", gap: 8 }}
                >
                    <div className="dim" style={{ fontSize: 12 }}>
                        Jump straight to this scene. A key or pad is instant; Program Change lets a
                        sequencer drive the setlist.
                    </div>
                    <button
                        onClick={() => {
                            setMenuOpen(false);
                            // Renderer clock, read at arm time -- see tryBind.
                            armedAt.current = useTelemetryStore.getState().latest?.lastControl?.ts ?? 0;
                            setArmed(true);
                        }}
                    >
                        Learn a key or pad…
                    </button>
                    <label className="row" style={{ gap: 6, alignItems: "center" }}>
                        <span className="dim" style={{ fontSize: 12, minWidth: 76 }}>Program #</span>
                        <input
                            type="number"
                            min={0}
                            max={127}
                            step={1}
                            style={{ width: 70 }}
                            value={scene.sceneSelect?.type === "program" ? scene.sceneSelect.number : ""}
                            placeholder="—"
                            onChange={(e) => {
                                const v = parseInt(e.target.value, 10);
                                bind(Number.isFinite(v) && v >= 0 && v <= 127 ? { type: "program", number: v } : null);
                            }}
                        />
                    </label>
                    {scene.sceneSelect && (
                        <button className="danger" onClick={() => { bind(null); setMenuOpen(false); }}>
                            Remove trigger
                        </button>
                    )}
                </div>
            )}
        </div>
    );
}
