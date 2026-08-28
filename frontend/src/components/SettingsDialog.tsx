// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
// Global device settings, opened from the ⚙️ in the topbar. Not per-show
// (those live in the scene editor) -- box-wide things the owner sets once:
// the login password, whether the on-screen setup/QR card shows on boot, and
// a MIDI key/knob bound to scene switching (learned live off the telemetry WS).

import { useEffect, useRef, useState } from "react";

import { api, ApiError } from "../api/client";
import type { SceneTrigger, Settings } from "../api/types";
import { useTelemetry, useTelemetryStore } from "../state/telemetryStore";
import PasswordDialog from "./PasswordDialog";
import SoftwareUpdate from "./SoftwareUpdate";
import ShowSwitchBindings from "./ShowSwitchBindings";

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

    // Writing the EEPROM takes ~13s. Without a visible pending state the dialog
    // just looks frozen, and the natural reaction -- hold the power button --
    // aborts the firmware write AND loses the setting. So: lock the control,
    // say what is happening, and say explicitly not to cut power.
    const [splashClips, setSplashClips] = useState<{ id: string; path: string }[] | null>(null);
    const [splashId, setSplashId] = useState<string | null>(null);
    useEffect(() => {
        api.splashCandidates()
            .then((c) => { setSplashClips(c.clips); setSplashId(c.selectedId); })
            .catch(() => setSplashClips([]));   // no picker rather than a broken one
    }, []);

    const setSplash = async (clipId: string) => {
        setError(null);
        setSplashId(clipId || null);            // optimistic
        try {
            await api.updateSettings({ splashClipId: clipId || null });
            const c = await api.splashCandidates();
            setSplashClips(c.clips); setSplashId(c.selectedId);
        } catch (err) {
            setError(err instanceof ApiError ? String(err.detail) : "Save failed");
            api.splashCandidates().then((c) => setSplashId(c.selectedId)).catch(() => {});
        }
    };

    const [netInstallBusy, setNetInstallBusy] = useState(false);
    const setNetInstall = async (on: boolean) => {
        setError(null);
        setNetInstallBusy(true);
        setSettings((s) => (s ? { ...s, netInstallPrompt: on } : s)); // optimistic
        try {
            const res = await api.updateSettings({ netInstallPrompt: on });
            setSettings((s) => (s ? { ...s, netInstallPrompt: res.netInstallPrompt } : s));
        } catch (err) {
            setError(err instanceof ApiError ? String(err.detail) : "Save failed");
            api.getSettings().then(setSettings).catch(() => {});
        } finally {
            setNetInstallBusy(false);
        }
    };

    const setThermalTransition = async (on: boolean) => {
        setError(null);
        setSettings((s) => (s ? { ...s, thermalTransition: on } : s)); // optimistic
        try {
            const res = await api.updateSettings({ thermalTransition: on });
            setSettings((s) => (s ? { ...s, thermalTransition: res.thermalTransition } : s));
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
                <div className="card dialog" onClick={(e) => e.stopPropagation()}>
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
                        <label className="row" style={{ gap: 8, alignItems: "center" }}>
                            <input
                                type="checkbox"
                                checked={settings?.thermalTransition ?? false}
                                disabled={!settings || !(settings?.thermalRescue ?? true)}
                                onChange={(e) => setThermalTransition(e.target.checked)}
                            />
                            <span>Use a transition when entering thermal safety</span>
                        </label>
                        <div className="dim" style={{ fontSize: 12 }}>
                            When thermal rescue drops the resolution mid-scene, mask it with the scene's transition
                            effect. Off (default): the scene just restarts quietly at the lower resolution — no random
                            static/tear. Scene entry always folds the reduced resolution into its own transition either way.
                        </div>
                    </section>

                    <ShowSwitchBindings
                        bindings={settings?.showSelect ?? []}
                        onChange={async (next) => {
                            setError(null);
                            setSettings((s) => (s ? { ...s, showSelect: next } : s));  // optimistic
                            try {
                                const res = await api.updateSettings({ showSelect: next });
                                setSettings((s) => (s ? { ...s, showSelect: res.showSelect } : s));
                            } catch (err) {
                                setError(err instanceof ApiError ? String(err.detail) : "Save failed");
                                api.getSettings().then(setSettings).catch(() => {});
                            }
                        }}
                    />

                    <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                        <strong>Boot splash</strong>
                        <label className="row" style={{ gap: 8, alignItems: "center" }}>
                            <span className="dim" style={{ minWidth: 52, fontSize: 12 }}>image</span>
                            <select
                                value={splashId ?? ""}
                                disabled={!splashClips}
                                onChange={(e) => setSplash(e.target.value)}
                                style={{ flex: 1 }}
                            >
                                <option value="">None — plain black</option>
                                {(splashClips ?? []).map((c) => (
                                    <option key={c.id} value={c.id}>
                                        {c.path.replace(/^clips\//, "")}
                                    </option>
                                ))}
                            </select>
                        </label>
                        {splashClips?.length === 0 && (
                            <div className="dim" style={{ fontSize: 12 }}>
                                No still images in the library yet — upload a PNG or JPG on the Clips page and it
                                will appear here.
                            </div>
                        )}
                        <div className="dim" style={{ fontSize: 12 }}>
                            Shown while the box starts up, from a few seconds after power-on right through to your
                            first scene — so a band logo here is what an audience sees if you ever have to restart
                            mid-set. <strong>Applies at the next boot.</strong>
                        </div>
                        <div className="dim" style={{ fontSize: 12 }}>
                            <strong>Suggested image:</strong> around <strong>1920×1080</strong> PNG or JPG. It is
                            scaled to fit and centred on black, so any shape works — it letterboxes rather than
                            cropping or stretching, and a square logo is fine. Smaller images are upscaled and can
                            look soft on a big panel; much larger ones just cost a little boot time. PNG
                            transparency composites over black.
                        </div>
                    </section>

                    {/* Only rendered where the board actually has the setting: the
                        backend reports null on anything that isn't a Pi with a
                        network-install prompt, and offering a control that cannot
                        work is worse than offering none. */}
                    {settings?.netInstallPrompt != null && (
                        <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                            <strong>Startup screen</strong>
                            <label className="row" style={{ gap: 8, alignItems: "center" }}>
                                <input
                                    type="checkbox"
                                    checked={settings.netInstallPrompt}
                                    disabled={netInstallBusy}
                                    onChange={(e) => setNetInstall(e.target.checked)}
                                />
                                <span>Show the Raspberry Pi network-install screen at power-on</span>
                            </label>
                            {netInstallBusy && (
                                <div className="warn" style={{ fontSize: 12 }}>
                                    Writing the bootloader firmware — takes about 15 seconds.
                                    <strong> Do not power off the box until this finishes.</strong>
                                </div>
                            )}
                            <div className="dim" style={{ fontSize: 12 }}>
                                The pink screen with a QR code that appears before this app starts. It is drawn by the
                                Pi's <em>bootloader</em>, so turning it off is the only way to get a fully black boot —
                                and it invites anyone watching to reinstall the operating system, which is rarely what
                                you want on a projector. Turning it off also removes network-install as a recovery
                                option for this board. <strong>Applies at the next power-on</strong>, and it is stored
                                in the board's firmware, so it survives re-flashing the card.
                            </div>
                        </section>
                    )}

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

                    <SoftwareUpdate />

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
