// THE shared parameter control (docs/videosynth-frontend.md): every param
// renders through this. Two independent binding buttons per performer
// feedback:
//   [MIDI]  -- unbound: arms Learn (binds the first knob turn or key press,
//              timestamp-guarded against stale events); bound: opens a
//              menu to re-learn or remove.
//   [♪]     -- opens the audio panel: pick a band (low/mid/high) and how
//              much of it touches this param. Audio rides ADDITIVELY on
//              top of whatever the knob/baseline sets (resolver semantics),
//              so both bindings can coexist on one param.
//
// Instant feedback: value changes call onChange (draft edit + debounced
// auto-save) AND fire a throttled param command so the monitor tracks the
// slider in real time.

import { useEffect, useRef, useState } from "react";

import { api } from "../api/client";
import type { AudioBand, Mapping, ParamSpec } from "../api/types";
import { useTelemetry, useTelemetryStore } from "../state/telemetryStore";

export interface MappableControlProps {
    label: string;
    spec: ParamSpec;
    value: number;
    onChange: (value: number) => void;
    /** Current CC/note mapping targeting this param, if any. */
    midiMapping?: { trigger: Mapping["trigger"] } | null;
    /** Current audioBand mapping targeting this param, if any. */
    audioMapping?: { band: AudioBand; amount: number } | null;
    onBindMidi: (trigger: Mapping["trigger"]) => void;
    onUnbindMidi: () => void;
    onBindAudio: (band: AudioBand, amount: number) => void;
    onUnbindAudio: () => void;
    /** Fired alongside onChange for renderer instant feedback. */
    sendPreview?: (value: number) => void;
}

export default function MappableControl(props: MappableControlProps) {
    const { label, spec, value, onChange, midiMapping, audioMapping } = props;
    const { onBindMidi, onUnbindMidi, onBindAudio, onUnbindAudio, sendPreview } = props;
    const [armed, setArmed] = useState(false);
    const [midiMenuOpen, setMidiMenuOpen] = useState(false);
    const [audioMenuOpen, setAudioMenuOpen] = useState(false);
    const armedAt = useRef(0);
    const telemetry = useTelemetry();
    const connected = useTelemetryStore((s) => s.connected);

    useEffect(() => {
        if (!armed || !telemetry?.lastControl) return;
        const { kind, number, ts } = telemetry.lastControl;
        if (kind !== "none" && ts > armedAt.current) {
            onBindMidi({ type: kind, number });
            setArmed(false);
        }
    }, [armed, telemetry, onBindMidi]);

    const arm = () => {
        armedAt.current = telemetry?.lastControl?.ts ?? 0;
        setArmed(true);
        setMidiMenuOpen(false);
        setAudioMenuOpen(false);
    };

    const midiBadge = midiMapping
        ? midiMapping.trigger.type === "cc"
            ? `CC ${midiMapping.trigger.number}`
            : `N${midiMapping.trigger.number}`
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
            ) : spec.type === "enum" && spec.options ? (
                <select
                    value={Math.round((value - spec.min) / (spec.max - spec.min) * (spec.options.length - 1))}
                    onChange={(e) => {
                        const index = parseInt(e.target.value, 10);
                        handleChange(spec.min + (index / (spec.options!.length - 1)) * (spec.max - spec.min));
                    }}
                    style={{ flex: 1 }}
                >
                    {spec.options.map((opt, i) => (
                        <option key={opt} value={i}>{opt}</option>
                    ))}
                </select>
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

            {/* MIDI binding button */}
            <div style={{ position: "relative" }}>
                <button
                    className="icon"
                    title={armed ? "Listening… (turn a knob or press a key)" : "Bind a MIDI knob or key"}
                    style={{
                        borderColor: armed ? "var(--warn)" : midiBadge ? "var(--accent)" : undefined,
                        color: armed ? "var(--warn)" : midiBadge ? "var(--accent)" : "var(--text-dim)",
                        minWidth: 56,
                    }}
                    onClick={() => (armed ? setArmed(false) : midiBadge ? setMidiMenuOpen(!midiMenuOpen) : arm())}
                >
                    {armed ? (connected ? "listen…" : "no WS!") : (midiBadge ?? "MIDI")}
                </button>
                {midiMenuOpen && (
                    <div
                        className="card"
                        style={{ position: "absolute", right: 0, top: "110%", zIndex: 20, width: 170, display: "flex", flexDirection: "column", gap: 6 }}
                    >
                        <button onClick={arm}>Re-learn…</button>
                        <button className="danger" onClick={() => { onUnbindMidi(); setMidiMenuOpen(false); }}>
                            Remove binding
                        </button>
                    </div>
                )}
            </div>

            {/* Audio binding button */}
            <div style={{ position: "relative" }}>
                <button
                    className="icon"
                    title="Drive this param from the music (low/mid/high band)"
                    style={{
                        borderColor: audioMapping ? "var(--ok)" : undefined,
                        color: audioMapping ? "var(--ok)" : "var(--text-dim)",
                        minWidth: 52,
                    }}
                    onClick={() => { setAudioMenuOpen(!audioMenuOpen); setMidiMenuOpen(false); }}
                >
                    {audioMapping ? `♪ ${audioMapping.band}` : "♪"}
                </button>
                {audioMenuOpen && (
                    <div
                        className="card"
                        style={{ position: "absolute", right: 0, top: "110%", zIndex: 20, width: 210, display: "flex", flexDirection: "column", gap: 8 }}
                    >
                        <div className="row" style={{ gap: 6 }}>
                            {(["low", "mid", "high"] as AudioBand[]).map((band) => (
                                <button
                                    key={band}
                                    style={{
                                        flex: 1,
                                        borderColor: audioMapping?.band === band ? "var(--ok)" : undefined,
                                        color: audioMapping?.band === band ? "var(--ok)" : undefined,
                                    }}
                                    onClick={() => onBindAudio(band, audioMapping?.amount ?? 0.5)}
                                >
                                    {band}
                                </button>
                            ))}
                        </div>
                        {audioMapping && (
                            <>
                                <div className="row" style={{ gap: 8 }}>
                                    <span className="dim" style={{ fontSize: 12, minWidth: 52 }}>amount</span>
                                    <input
                                        type="range"
                                        min={0.05}
                                        max={1}
                                        step={0.05}
                                        value={audioMapping.amount}
                                        onChange={(e) => onBindAudio(audioMapping.band, parseFloat(e.target.value))}
                                        style={{ flex: 1 }}
                                    />
                                    <span className="dim" style={{ fontSize: 12 }}>{audioMapping.amount.toFixed(2)}</span>
                                </div>
                                <button className="danger" onClick={() => { onUnbindAudio(); setAudioMenuOpen(false); }}>
                                    Remove audio binding
                                </button>
                            </>
                        )}
                        {!audioMapping && (
                            <div className="dim" style={{ fontSize: 12 }}>
                                Pick a band -- its level rides on top of the knob/baseline value.
                            </div>
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
