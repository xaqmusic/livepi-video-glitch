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
    useTelemetry();  // keep the shared WS alive while any control is mounted
    const connected = useTelemetryStore((s) => s.connected);

    // onBindMidi is an inline closure from the parent -- new identity every
    // render. Kept in a ref so the armed subscription below NEVER depends
    // on render-cycle identities.
    const onBindMidiRef = useRef(onBindMidi);
    onBindMidiRef.current = onBindMidi;

    // Learn binds via an IMPERATIVE store subscription, not a render-cycle
    // effect. The old form -- useEffect([armed, telemetry, onBindMidi])
    // calling setState -- had dependencies that change on every telemetry
    // frame and every parent render; at the bind moment (store write +
    // auto-save + dozens of sibling controls re-rendering) React saw a
    // long enough same-tick update chain to throw "Maximum update depth
    // exceeded" (error #185, caught live during a learn at the rig). This
    // form subscribes once per arming, fires at most once, and no object
    // identity anywhere can re-trigger it.
    useEffect(() => {
        if (!armed) return;
        let done = false;
        const maybeBind = (latest: ReturnType<typeof useTelemetryStore.getState>["latest"]) => {
            const lc = latest?.lastControl;
            if (done || !lc || lc.kind === "none" || lc.ts <= armedAt.current) return;
            done = true;
            setArmed(false);
            onBindMidiRef.current({ type: lc.kind, number: lc.number });
        };
        maybeBind(useTelemetryStore.getState().latest);
        const unsub = useTelemetryStore.subscribe((s) => maybeBind(s.latest));
        return unsub;
    }, [armed]);

    const arm = () => {
        // Freshest timestamp straight from the store (not a render
        // closure): only events NEWER than the arming moment may bind.
        armedAt.current = useTelemetryStore.getState().latest?.lastControl?.ts ?? 0;
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

    // Touch context slider: on a phone the inline range slider is a tiny target,
    // so press-and-hold the param NAME to pop a fat drag track under the finger,
    // slide to set the value, lift to dismiss. Gated to touch/pen pointers so
    // the mouse/desktop path (the inline slider) is untouched. Float params only
    // -- toggles/enums are already fine to tap.
    const isSlider = spec.type !== "toggle" && spec.type !== "enum";
    const [ctxOpen, setCtxOpen] = useState(false);
    const trackRef = useRef<HTMLDivElement | null>(null);

    const valueFromClientX = (clientX: number): number => {
        const track = trackRef.current;
        if (!track) return value;
        const r = track.getBoundingClientRect();
        const t = Math.min(1, Math.max(0, (clientX - r.left) / r.width));
        return spec.min + t * (spec.max - spec.min);
    };
    const onLabelPointerDown = (e: React.PointerEvent) => {
        if (!isSlider || e.pointerType === "mouse") return;
        e.preventDefault();
        (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
        setCtxOpen(true);
    };
    const onLabelPointerMove = (e: React.PointerEvent) => {
        if (!ctxOpen) return;
        handleChange(valueFromClientX(e.clientX));
    };
    const onLabelPointerUp = (e: React.PointerEvent) => {
        if (!ctxOpen) return;
        (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId);
        setCtxOpen(false);
    };
    const fillPct = ((value - spec.min) / (spec.max - spec.min)) * 100;

    return (
        <div className="row mappable" style={{ gap: 8 }}>
            <div className="mappable-label">
                <span
                    style={isSlider ? { touchAction: "none", userSelect: "none", cursor: "ew-resize" } : undefined}
                    title={isSlider ? "Hold and drag to adjust (touch)" : undefined}
                    onPointerDown={onLabelPointerDown}
                    onPointerMove={onLabelPointerMove}
                    onPointerUp={onLabelPointerUp}
                >
                    {label}
                </span>
                {ctxOpen && isSlider && (
                    <div
                        className="card"
                        style={{ position: "absolute", left: 0, top: "110%", zIndex: 30, width: 240, padding: 10 }}
                    >
                        <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12, marginBottom: 6 }}>
                            <span>{label}</span>
                            <span className="dim">{value.toFixed(2)}</span>
                        </div>
                        <div
                            ref={trackRef}
                            style={{ position: "relative", height: 30, borderRadius: 6, background: "var(--bg-input)", overflow: "hidden" }}
                        >
                            <div style={{ position: "absolute", inset: 0, width: `${fillPct}%`, background: "var(--accent)", opacity: 0.55 }} />
                        </div>
                    </div>
                )}
            </div>
            {spec.type === "toggle" ? (
                <input
                    type="checkbox"
                    checked={value > 0.5}
                    onChange={(e) => handleChange(e.target.checked ? 1 : 0)}
                />
            ) : spec.type === "enum" && spec.options ? (
                <select
                    className="mappable-slider"
                    value={Math.round((value - spec.min) / (spec.max - spec.min) * (spec.options.length - 1))}
                    onChange={(e) => {
                        const index = parseInt(e.target.value, 10);
                        handleChange(spec.min + (index / (spec.options!.length - 1)) * (spec.max - spec.min));
                    }}
                >
                    {spec.options.map((opt, i) => (
                        <option key={opt} value={i}>{opt}</option>
                    ))}
                </select>
            ) : (
                <>
                    <input
                        className="mappable-slider"
                        type="range"
                        min={spec.min}
                        max={spec.max}
                        step={(spec.max - spec.min) / 200}
                        value={value}
                        onChange={(e) => handleChange(parseFloat(e.target.value))}
                    />
                    <span className="dim mappable-value">
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
