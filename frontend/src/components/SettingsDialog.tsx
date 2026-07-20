// Global device settings, opened from the ⚙️ in the topbar. Not per-show
// (those live in the scene editor) -- box-wide things the owner sets once:
// the login password, whether the on-screen setup/QR card shows on boot, and
// (Pass 2) a MIDI key/knob bound to scene switching.

import { useEffect, useState } from "react";

import { api, ApiError } from "../api/client";
import type { Settings } from "../api/types";
import PasswordDialog from "./PasswordDialog";

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

    return (
        <>
            <div className="dialog-backdrop" onClick={onClose}>
                <div className="card dialog" onClick={(e) => e.stopPropagation()} style={{ minWidth: 340 }}>
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
