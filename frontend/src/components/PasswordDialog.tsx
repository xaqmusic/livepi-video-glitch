// Change the shared password from the topbar (docs/videosynth-backend.md
// keeps auth to one shared password -- this just lets it be set from the
// UI instead of backend/.env). The backend persists it in the data dir,
// so it survives deploys and reboots.

import { useState } from "react";

import { api, ApiError } from "../api/client";

export default function PasswordDialog({ onClose }: { onClose: () => void }) {
    const [current, setCurrent] = useState("");
    const [next, setNext] = useState("");
    const [confirm, setConfirm] = useState("");
    const [status, setStatus] = useState<string | null>(null);
    const [busy, setBusy] = useState(false);

    const submit = async (e: React.FormEvent) => {
        e.preventDefault();
        if (next !== confirm) {
            setStatus("New passwords don't match");
            return;
        }
        setBusy(true);
        setStatus(null);
        try {
            await api.changePassword(current, next);
            setStatus("Password updated ✓");
            setTimeout(onClose, 900);
        } catch (err) {
            setStatus(err instanceof ApiError ? String(err.detail) : "Failed");
        } finally {
            setBusy(false);
        }
    };

    return (
        <div className="dialog-backdrop" onClick={onClose}>
            <form className="card dialog" onClick={(e) => e.stopPropagation()} onSubmit={submit}>
                <h3 style={{ margin: 0 }}>Change password</h3>
                <input
                    type="password"
                    placeholder="Current password"
                    value={current}
                    onChange={(e) => setCurrent(e.target.value)}
                    autoFocus
                />
                <input type="password" placeholder="New password" value={next} onChange={(e) => setNext(e.target.value)} />
                <input
                    type="password"
                    placeholder="Repeat new password"
                    value={confirm}
                    onChange={(e) => setConfirm(e.target.value)}
                />
                {status && <div className={status.endsWith("✓") ? "dim" : "warn"}>{status}</div>}
                <div className="row" style={{ justifyContent: "flex-end" }}>
                    <button type="button" onClick={onClose}>Cancel</button>
                    <button type="submit" disabled={busy || !current || !next}>Save</button>
                </div>
            </form>
        </div>
    );
}
