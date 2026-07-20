// Global device settings, opened from the ⚙️ in the topbar. Not per-show
// (those live in the scene editor) -- box-wide things the owner sets once:
// the login password, whether the on-screen setup/QR card shows on boot, and
// a MIDI key/knob bound to scene switching (learned live off the telemetry WS).

import { useEffect, useRef, useState } from "react";

import { api, ApiError } from "../api/client";
import type { SceneTrigger, Settings } from "../api/types";
import { useTelemetry, useTelemetryStore } from "../state/telemetryStore";
import PasswordDialog from "./PasswordDialog";

// One "bind a control to a scene action" row. Same Learn mechanism as
// MappableControl: arm, then bind the first CC/note that arrives NEWER than the
// arming instant (timestamp-guarded against a stale latched event), via an
// imperative store subscription rather than a render-cycle effect.
function SceneLearnRow({
    label,
    binding,
    onLearn,
    onClear,
}: {
    label: string;
    binding: SceneTrigger | null | undefined;
    onLearn: (t: SceneTrigger) => void;
    onClear: () => void;
}) {
    const [armed, setArmed] = useState(false);
    const armedAt = useRef(0);
    useTelemetry(); // keep the shared telemetry WS alive while this row is mounted
    const connected = useTelemetryStore((s) => s.connected);
    const onLearnRef = useRef(onLearn);
    onLearnRef.current = onLearn;

    useEffect(() => {
        if (!armed) return;
        let done = false;
        const maybeBind = (latest: ReturnType<typeof useTelemetryStore.getState>["latest"]) => {
            const lc = latest?.lastControl;
            if (done || !lc || lc.kind === "none" || lc.ts <= armedAt.current) return;
            done = true;
            setArmed(false);
            onLearnRef.current({ type: lc.kind, number: lc.number });
        };
        maybeBind(useTelemetryStore.getState().latest);
        return useTelemetryStore.subscribe((s) => maybeBind(s.latest));
    }, [armed]);

    const arm = () => {
        armedAt.current = useTelemetryStore.getState().latest?.lastControl?.ts ?? 0;
        setArmed(true);
    };

    const badge = binding ? (binding.type === "cc" ? `CC ${binding.number}` : `Note ${binding.number}`) : null;

    return (
        <div className="row" style={{ gap: 8, alignItems: "center" }}>
            <span style={{ flex: 1 }}>{label}</span>
            <button
                className="icon"
                style={{
                    minWidth: 88,
                    borderColor: armed ? "var(--warn)" : badge ? "var(--accent)" : undefined,
                    color: armed ? "var(--warn)" : badge ? "var(--accent)" : "var(--text-dim)",
                }}
                title={armed ? "Listening… press a key or turn a knob" : "Bind a MIDI key or knob"}
                onClick={() => (armed ? setArmed(false) : arm())}
            >
                {armed ? (connected ? "listen…" : "no WS!") : (badge ?? "Learn")}
            </button>
            {badge && (
                <button className="danger icon" title="Remove binding" onClick={onClear}>✕</button>
            )}
        </div>
    );
}

export default function SettingsDialog({
    onClose,
    onPasswordChanged,
    firstRunPassword = false,
}: {
    onClose: () => void;
    /** Bubbles up so the App can clear the first-run password nudge. */
    onPasswordChanged?: () => void;
    /** The box is still on its printed code -- the password sub-dialog then
     *  asks only for the new one (the session already proves the code). */
    firstRunPassword?: boolean;
}) {
    const [settings, setSettings] = useState<Settings | null>(null);
    const [pwOpen, setPwOpen] = useState(false);
    const [error, setError] = useState<string | null>(null);

    useEffect(() => {
        api.getSettings().then(setSettings).catch(() => setError("Couldn't load settings"));
    }, []);

    const setCardOnBoot = async (show: boolean) => {
        setError(null);
        setSettings((s) => (s ? { ...s, showCardOnBoot: show } : s)); // optimistic
        try {
            const res = await api.updateSettings({ showCardOnBoot: show });
            setSettings((s) => (s ? { ...s, showCardOnBoot: res.showCardOnBoot } : s));
        } catch (err) {
            setError(err instanceof ApiError ? String(err.detail) : "Save failed");
            api.getSettings().then(setSettings).catch(() => {}); // resync from truth
        }
    };

    const setThermal = async (on: boolean) => {
        setError(null);
        setSettings((s) => (s ? { ...s, thermalRescue: on } : s)); // optimistic
        try {
            const res = await api.updateSettings({ thermalRescue: on });
            setSettings((s) => (s ? { ...s, thermalRescue: res.thermalRescue } : s));
        } catch (err) {
            setError(err instanceof ApiError ? String(err.detail) : "Save failed");
            api.getSettings().then(setSettings).catch(() => {});
        }
    };

    const setAudioAutoGain = async (on: boolean) => {
        setError(null);
        setSettings((s) => (s ? { ...s, audioAutoGain: on } : s)); // optimistic
        try {
            const res = await api.updateSettings({ audioAutoGain: on });
            setSettings((s) => (s ? { ...s, audioAutoGain: res.audioAutoGain } : s));
        } catch (err) {
            setError(err instanceof ApiError ? String(err.detail) : "Save failed");
            api.getSettings().then(setSettings).catch(() => {});
        }
    };

    // Slider: reflect the drag instantly (optimistic) but debounce the save, so
    // dragging doesn't fire a POST per step -- the renderer only reacts on the
    // write to settings.json anyway.
    const smoothingTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
    const setAudioSmoothing = (v: number) => {
        setError(null);
        setSettings((s) => (s ? { ...s, audioSmoothing: v } : s));
        if (smoothingTimer.current) clearTimeout(smoothingTimer.current);
        smoothingTimer.current = setTimeout(async () => {
            try {
                const res = await api.updateSettings({ audioSmoothing: v });
                setSettings((s) => (s ? { ...s, audioSmoothing: res.audioSmoothing } : s));
            } catch (err) {
                setError(err instanceof ApiError ? String(err.detail) : "Save failed");
                api.getSettings().then(setSettings).catch(() => {});
            }
        }, 250);
    };

    const setBinding = async (field: "sceneAdvance" | "sceneBack", value: SceneTrigger | null) => {
        setError(null);
        setSettings((s) => (s ? { ...s, [field]: value } : s)); // optimistic
        try {
            const res = await api.updateSettings({ [field]: value } as Partial<Settings>);
            setSettings((s) => (s ? { ...s, sceneAdvance: res.sceneAdvance, sceneBack: res.sceneBack } : s));
        } catch (err) {
            setError(err instanceof ApiError ? String(err.detail) : "Save failed");
            api.getSettings().then(setSettings).catch(() => {});
        }
    };

    return (
        <>
            <div className="dialog-backdrop" onClick={onClose}>
                <div className="card dialog" onClick={(e) => e.stopPropagation()} style={{ minWidth: 360 }}>
                    <h3 style={{ margin: 0 }}>Settings</h3>

                    <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                        <strong>Password</strong>
                        <div className="row" style={{ justifyContent: "space-between", alignItems: "center", gap: 12 }}>
                            <span className="dim" style={{ fontSize: 12 }}>
                                The box's login — also its console/SSH login and hotspot key.
                            </span>
                            <button onClick={() => setPwOpen(true)}>Change…</button>
                        </div>
                    </section>

                    <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                        <strong>Setup card</strong>
                        <label className="row" style={{ gap: 8, alignItems: "center" }}>
                            <input
                                type="checkbox"
                                checked={settings?.showCardOnBoot ?? false}
                                disabled={!settings}
                                onChange={(e) => setCardOnBoot(e.target.checked)}
                            />
                            <span>Show the connection / QR card on every boot</span>
                        </label>
                        <div className="dim" style={{ fontSize: 12 }}>
                            Turns itself off after the first login. Re-enable to help someone connect at a new venue.
                        </div>
                    </section>

                    <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                        <strong>Scene switch (MIDI)</strong>
                        <div className="dim" style={{ fontSize: 12 }}>
                            Bind a key or knob on your controller to change scenes hands-free.
                        </div>
                        <SceneLearnRow
                            label="Next scene"
                            binding={settings?.sceneAdvance}
                            onLearn={(t) => setBinding("sceneAdvance", t)}
                            onClear={() => setBinding("sceneAdvance", null)}
                        />
                        <SceneLearnRow
                            label="First scene"
                            binding={settings?.sceneBack}
                            onLearn={(t) => setBinding("sceneBack", t)}
                            onClear={() => setBinding("sceneBack", null)}
                        />
                    </section>

                    <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                        <strong>Performance</strong>
                        <label className="row" style={{ gap: 8, alignItems: "center" }}>
                            <input
                                type="checkbox"
                                checked={settings?.thermalRescue ?? true}
                                disabled={!settings}
                                onChange={(e) => setThermal(e.target.checked)}
                            />
                            <span>Thermal rescue — drop resolution when the box overheats</span>
                        </label>
                        <div className="dim" style={{ fontSize: 12 }}>
                            A global safety cap: pulls high-resolution scenes down as the SoC heats up, so a heavy
                            set degrades gracefully instead of stuttering to black. Per-scene resolution lives in the
                            scene editor.
                        </div>
                    </section>

                    <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                        <strong>Audio reactivity</strong>
                        <label className="row" style={{ gap: 8, alignItems: "center", justifyContent: "space-between" }}>
                            <span>Level smoothing</span>
                            <input
                                type="range"
                                min={0}
                                max={0.95}
                                step={0.05}
                                value={settings?.audioSmoothing ?? 0.6}
                                disabled={!settings}
                                onChange={(e) => setAudioSmoothing(parseFloat(e.target.value))}
                                style={{ flex: 1, maxWidth: 200 }}
                            />
                        </label>
                        <div className="dim" style={{ fontSize: 12 }}>
                            Lower = snappier reaction (more jitter); higher = steadier (more lag). Smooths the overall
                            audio level only — the low/mid/high bands stay fast regardless.
                        </div>
                        <label className="row" style={{ gap: 8, alignItems: "center" }}>
                            <input
                                type="checkbox"
                                checked={settings?.audioAutoGain ?? true}
                                disabled={!settings}
                                onChange={(e) => setAudioAutoGain(e.target.checked)}
                            />
                            <span>Automatic level adjustment</span>
                        </label>
                        <div className="dim" style={{ fontSize: 12 }}>
                            On: auto-normalizes to the room, so quiet and loud both fill the range (good for a
                            microphone). Off: unity gain — no auto-adjustment at all; you set the level entirely with
                            the Pisound's own input gain knob (best for a line source patched in).
                        </div>
                    </section>

                    {error && <div className="warn">{error}</div>}
                    <div className="row" style={{ justifyContent: "flex-end" }}>
                        <button onClick={onClose}>Close</button>
                    </div>
                </div>
            </div>

            {pwOpen && (
                <PasswordDialog
                    firstRun={firstRunPassword}
                    onChanged={() => onPasswordChanged?.()}
                    onClose={() => setPwOpen(false)}
                />
            )}
        </>
    );
}
