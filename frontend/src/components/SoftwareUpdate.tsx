// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
// "Software update" section of the Settings dialog (appliance only). Upload a
// .tar.zst bundle; the box verifies it, swaps the live app tree by symlink,
// restarts, and health-gates itself -- auto-rolling-back if the new version
// doesn't come up. Because that restart kills THIS backend mid-apply, we can't
// get the result in the upload response: we poll /api/update/status (tolerating
// the connection dropping while the backend restarts) until lastApply lands on
// a terminal status. Hidden entirely on a dev box (available:false).

import { useEffect, useRef, useState } from "react";

import { api, ApiError } from "../api/client";
import type { UpdateStatus } from "../api/types";

type Phase = "idle" | "uploading" | "applying" | "done" | "error";

export default function SoftwareUpdate() {
    const [status, setStatus] = useState<UpdateStatus | null>(null);
    const [phase, setPhase] = useState<Phase>("idle");
    const [msg, setMsg] = useState("");
    const fileRef = useRef<HTMLInputElement>(null);

    useEffect(() => {
        api.updateStatus().then(setStatus).catch(() => setStatus({ available: false }));
    }, []);

    // Poll until the apply/rollback reaches a terminal state. The backend
    // restarts partway through, so updateStatus() rejects for a few seconds --
    // swallow those and keep polling rather than treating them as failure.
    const pollUntilDone = async () => {
        const deadline = Date.now() + 90_000;
        while (Date.now() < deadline) {
            await new Promise((r) => setTimeout(r, 3000));
            let s: UpdateStatus | null = null;
            try {
                s = await api.updateStatus();
            } catch {
                continue; // backend mid-restart; try again
            }
            setStatus(s);
            const st = s.lastApply?.status;
            if (st === "ok") {
                setPhase("done");
                setMsg(s.lastApply?.message ?? "Update complete.");
                return;
            }
            if (st === "rolled-back" || st === "failed") {
                setPhase("error");
                setMsg(s.lastApply?.message ?? "Update failed and was rolled back.");
                return;
            }
        }
        setPhase("error");
        setMsg("Timed out waiting for the update to finish — check the box's screen.");
    };

    const onFile = async (file: File) => {
        if (!file.name.endsWith(".tar.zst")) {
            setPhase("error");
            setMsg("That isn't a LivePi update bundle (expected a .tar.zst file).");
            return;
        }
        setPhase("uploading");
        setMsg(`Uploading ${file.name}…`);
        try {
            await api.uploadUpdate(file);
            setPhase("applying");
            setMsg("Installing and restarting — the picture may blink. Keep this page open.");
            await pollUntilDone();
        } catch (err) {
            setPhase("error");
            setMsg(err instanceof ApiError ? String(err.detail) : "Upload failed.");
        }
    };

    const rollback = async () => {
        if (!window.confirm("Roll back to the previous version and restart?")) return;
        setPhase("applying");
        setMsg("Rolling back — the picture may blink. Keep this page open.");
        try {
            await api.rollbackUpdate();
            await pollUntilDone();
        } catch (err) {
            setPhase("error");
            setMsg(err instanceof ApiError ? String(err.detail) : "Rollback failed.");
        }
    };

    // Dev box / no updatable partition -> don't render the section at all.
    if (status && !status.available) return null;

    const busy = phase === "uploading" || phase === "applying";
    const cur = status?.current?.version ?? "…";
    const lastGood = status?.lastGood?.version;
    const canRollback = !!lastGood && lastGood !== cur;

    return (
        <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
            <strong>Software update</strong>
            <div className="dim" style={{ fontSize: 12 }}>
                Current version <code>{cur}</code>
                {status?.pending ? " — trying a new version…" : ""}
            </div>
            <input
                ref={fileRef}
                type="file"
                accept=".zst,.tar.zst,application/zstd"
                style={{ display: "none" }}
                onChange={(e) => {
                    const f = e.target.files?.[0];
                    e.target.value = ""; // allow re-picking the same file
                    if (f) onFile(f);
                }}
            />
            <div className="row" style={{ gap: 8 }}>
                <button disabled={busy} onClick={() => fileRef.current?.click()}>
                    {busy ? "Working…" : "Upload update…"}
                </button>
                {canRollback && (
                    <button className="danger" disabled={busy} onClick={rollback}>
                        Roll back to {lastGood}
                    </button>
                )}
            </div>
            {msg && (
                <div className={phase === "error" ? "warn" : "dim"} style={{ fontSize: 12 }}>
                    {msg}
                </div>
            )}
            <div className="dim" style={{ fontSize: 12 }}>
                A version that doesn't start up healthy is rolled back automatically, so an update can't
                brick the box.
            </div>
            {status?.deviceCode && (
                <div className="dim" style={{ fontSize: 12, borderTop: "1px solid var(--border)", paddingTop: 8 }}>
                    Box terminal / SSH login &amp; Wi-Fi hotspot key:{" "}
                    <code style={{ userSelect: "all" }}>{status.deviceCode}</code>
                    <br />
                    This is the original device code — unchanged when you set your web password. Use it for{" "}
                    <code>ssh</code>/<code>sudo</code> on the Pi and to join the box's hotspot; your web login
                    is the separate password you set here.
                </div>
            )}
        </section>
    );
}
