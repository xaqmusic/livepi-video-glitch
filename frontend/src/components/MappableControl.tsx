// THE shared parameter control (docs/videosynth-frontend.md): every param
// in the app renders through this -- slider/toggle per spec, plus the MIDI
// icon that arms Learn. Building it once is what makes "MIDI learn on
// every parameter" true in practice: a new effect in the manifest gets
// Learn for free.
//
// Learn flow: arm -> shared telemetry WS -> bind the first CC event whose
// timestamp is NEWER than arming time (the status file always carries the
// LAST cc seen -- without the ts guard it would instantly bind a stale
// knob wiggle from minutes ago). Clicking again re-arms (re-map in one
// click -- the common setup gesture is "try a different knob").
//
// Instant feedback: value changes call onChange (which edits the draft +
// debounced auto-save) AND fire a param command at the renderer, so the
// monitor tracks the slider in real time while the save catches up.

import { useEffect, useRef, useState } from "react";

import { api } from "../api/client";
import type { AudioBand, Mapping, ParamSpec } from "../api/types";
import { useTelemetry, useTelemetryStore } from "../state/telemetryStore";

export interface MappableControlProps {
    label: string;
    spec: ParamSpec;
    value: number;
    onChange: (value: number) => void;
    /** Mapping currently targeting this param (for the badge), if any. */
    mapping?: { trigger: Mapping["trigger"] } | null;
    /** Create/replace a mapping binding this param to a CC or band. */
    onBind: (trigger: Mapping["trigger"]) => void;
    onUnbind?: () => void;
    /** Fired alongside onChange for renderer instant feedback. */
    sendPreview?: (value: number) => void;
}

export default function MappableControl(props: MappableControlProps) {
    const { label, spec, value, onChange, mapping, onBind, onUnbind, sendPreview } = props;
    const [armed, setArmed] = useState(false);
    const [menuOpen, setMenuOpen] = useState(false);
    const armedAt = useRef(0);
    const telemetry = useTelemetry();
    const connected = useTelemetryStore((s) => s.connected);

    useEffect(() => {
        if (!armed || !telemetry?.lastControl) return;
        const { kind, number, ts } = telemetry.lastControl;
        if (kind !== "none" && ts > armedAt.current) {
            onBind({ type: kind, number });
            setArmed(false);
        }
    }, [armed, telemetry, onBind]);

    const arm = () => {
        // Renderer clock: compare against the telemetry we already have so
        // "newer than arming" is measured in the renderer's own time base.
        armedAt.current = telemetry?.lastControl?.ts ?? 0;
        setArmed(true);
        setMenuOpen(false);
    };

    const badge = mapping
        ? mapping.trigger.type === "cc"
            ? `CC ${mapping.trigger.number}`
            : mapping.trigger.type === "note"
              ? `N${mapping.trigger.number}`
              : `♪ ${mapping.trigger.band}`
        : null;

    const handleChange = (v: number) => {
        onChange(v);
        sendPreview?.(v);
    };

    return (
        <div className="row" style={{ gap: 8 }}>
            <span style={{ minWidth: 150 }}>{label}</span>
            {spec.type === "toggle" ? (
                <input
                    type="checkbox"
                    checked={value > 0.5}
                    onChange={(e) => handleChange(e.target.checked ? 1 : 0)}
                />
            ) : (
                <>
                    <input
                        type="range"
                        min={spec.min}
                        max={spec.max}
                        step={(spec.max - spec.min) / 200}
                        value={value}
                        onChange={(e) => handleChange(parseFloat(e.target.value))}
                        style={{ flex: 1 }}
                    />
                    <span className="dim" style={{ minWidth: 38, textAlign: "right" }}>
                        {value.toFixed(2)}
                    </span>
                </>
            )}
            <div style={{ position: "relative" }}>
                <button
                    className="icon"
                    title={armed ? "Listening… (turn a knob or press a key)" : "Map to a MIDI knob, key, or audio band"}
                    style={{
                        borderColor: armed ? "var(--warn)" : badge ? "var(--accent)" : undefined,
                        color: armed ? "var(--warn)" : badge ? "var(--accent)" : "var(--text-dim)",
                        minWidth: 56,
                    }}
                    onClick={() => (armed ? setArmed(false) : badge ? setMenuOpen(!menuOpen) : arm())}
                >
                    {armed ? (connected ? "listen…" : "no WS!") : (badge ?? "MIDI")}
                </button>
                {menuOpen && (
                    <div
                        className="card"
                        style={{ position: "absolute", right: 0, top: "110%", zIndex: 20, width: 180, display: "flex", flexDirection: "column", gap: 6 }}
                    >
                        <button onClick={arm}>Re-learn CC…</button>
                        {(["low", "mid", "high"] as AudioBand[]).map((band) => (
                            <button key={band} onClick={() => { onBind({ type: "audioBand", band }); setMenuOpen(false); }}>
                                Bind {band} band
                            </button>
                        ))}
                        {onUnbind && (
                            <button className="danger" onClick={() => { onUnbind(); setMenuOpen(false); }}>
                                Remove mapping
                            </button>
                        )}
                    </div>
                )}
            </div>
        </div>
    );
}

/** Instant-feedback helper: throttled param command to the renderer. */
export function makePreviewSender(sceneId: string, targetPath: string): (value: number) => void {
    let last = 0;
    let pendingValue: number | null = null;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const send = (value: number) => {
        void api
            .command({ type: "param", sceneId, targetPath, value })
            .catch(() => {/* renderer offline -- auto-save still lands it */});
    };

    return (value: number) => {
        const now = Date.now();
        if (now - last > 33) {
            last = now;
            send(value);
        } else {
            pendingValue = value;
            if (!timer) {
                timer = setTimeout(() => {
                    timer = null;
                    if (pendingValue !== null) send(pendingValue);
                    pendingValue = null;
                }, 40);
            }
        }
    };
}
