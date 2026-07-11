// WiFi provisioning: put the box on a venue network from a phone, no
// terminal. Backed by /api/network/* (NetworkManager via nmcli). The box is
// its own control-network AP by default; joining a venue network switches
// the single radio off that AP -- flagged in the UI, since a phone connected
// via the hotspot will drop when it happens.

import { useCallback, useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";

import { api, ApiError } from "../api/client";
import type { NetworkStatus, WifiNetwork } from "../api/types";

function signalMeter(signal: number): string {
    const filled = Math.min(4, Math.max(1, Math.round(signal / 25)));
    return "▰".repeat(filled) + "▱".repeat(4 - filled);
}

const isOpen = (security: string) => !security || security === "open";

export default function Network() {
    const [status, setStatus] = useState<NetworkStatus | null>(null);
    const [networks, setNetworks] = useState<WifiNetwork[]>([]);
    const [scanning, setScanning] = useState(false);
    const [selected, setSelected] = useState<string | null>(null);
    const [password, setPassword] = useState("");
    const [busy, setBusy] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [notice, setNotice] = useState<string | null>(null);
    const navigate = useNavigate();

    const onErr = useCallback(
        (e: unknown) => {
            if (e instanceof ApiError && e.status === 401) navigate("/login");
            else setError(e instanceof ApiError ? String(e.detail) : String(e));
        },
        [navigate],
    );

    const refreshStatus = useCallback(async () => {
        try {
            setStatus(await api.networkStatus());
        } catch (e) {
            onErr(e);
        }
    }, [onErr]);

    const scan = useCallback(async () => {
        setScanning(true);
        setError(null);
        try {
            setNetworks((await api.networkScan()).networks);
        } catch (e) {
            onErr(e);
        } finally {
            setScanning(false);
        }
    }, [onErr]);

    useEffect(() => {
        void refreshStatus();
        void scan();
    }, [refreshStatus, scan]);

    const join = async (ssid: string, security: string, hidden = false) => {
        setBusy(true);
        setError(null);
        setNotice(`Joining "${ssid}"…`);
        try {
            await api.networkConnect(ssid, isOpen(security) ? "" : password, hidden);
            setNotice(`Connected to "${ssid}".`);
            setSelected(null);
            setPassword("");
            await refreshStatus();
            await scan();
        } catch (e) {
            setNotice(null);
            onErr(e);
        } finally {
            setBusy(false);
        }
    };

    const forget = async (ssid: string) => {
        if (!confirm(`Forget "${ssid}" and drop back to the box's own hotspot?`)) return;
        setBusy(true);
        setError(null);
        try {
            await api.networkForget(ssid);
            setNotice(`Forgot "${ssid}". Back on the control hotspot.`);
            await refreshStatus();
        } catch (e) {
            onErr(e);
        } finally {
            setBusy(false);
        }
    };

    const joinHidden = async () => {
        const ssid = prompt("Network name (SSID):");
        if (!ssid) return;
        const pw = prompt(`Password for "${ssid}" (blank for open):`) ?? "";
        setPassword(pw);
        await join(ssid.trim(), pw ? "WPA2" : "open", true);
    };

    return (
        <div className="page">
            <h2 style={{ marginTop: 0 }}>Network</h2>

            {status && (
                <div className="card" style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                    <div className="row" style={{ justifyContent: "space-between" }}>
                        <strong>{status.hostname}</strong>
                        <span style={{ color: status.online ? "var(--ok)" : "var(--dim, #888)", fontSize: 12 }}>
                            {status.online ? "● online" : "○ no internet"}
                        </span>
                    </div>
                    {status.ethernet.connected && <div className="dim">Ethernet — {status.ethernet.ip}</div>}
                    {status.ap.active && (
                        <div className="dim">
                            Hotspot “{status.ap.ssid}” — reach the box at http://{status.ap.ip}
                        </div>
                    )}
                    {status.wifiClient.connected && (
                        <div className="row" style={{ justifyContent: "space-between" }}>
                            <span style={{ color: "var(--ok)" }}>
                                WiFi “{status.wifiClient.ssid}” — {status.wifiClient.ip}
                            </span>
                            <button className="danger" disabled={busy} onClick={() => void forget(status.wifiClient.ssid!)}>
                                Forget
                            </button>
                        </div>
                    )}
                </div>
            )}

            {error && <div className="error">{error}</div>}
            {notice && <div className="dim">{notice}</div>}

            <div className="row" style={{ justifyContent: "space-between", marginTop: 8 }}>
                <h3 style={{ margin: 0 }}>Available networks</h3>
                <div className="row">
                    <button onClick={() => void joinHidden()}>Other…</button>
                    <button className="primary" disabled={scanning} onClick={() => void scan()}>
                        {scanning ? "Scanning…" : "Rescan"}
                    </button>
                </div>
            </div>

            <div className="dim" style={{ fontSize: 12 }}>
                Joining a network switches the box off its own hotspot. If you connected via the hotspot, you'll need to
                rejoin the box on that network afterward.
            </div>

            {networks.map((n) => (
                <div key={n.ssid} className="card" style={{ display: "flex", flexDirection: "column", gap: 8 }}>
                    <div className="row" style={{ justifyContent: "space-between" }}>
                        <span style={{ fontWeight: 600 }}>
                            {n.ssid} {isOpen(n.security) ? "" : "🔒"} {n.inUse ? "✓" : ""}
                        </span>
                        <span className="row" style={{ gap: 10 }}>
                            <span className="dim" title={`${n.signal}%`} style={{ letterSpacing: 1 }}>
                                {signalMeter(n.signal)}
                            </span>
                            <button
                                disabled={busy}
                                onClick={() => {
                                    setError(null);
                                    if (isOpen(n.security)) void join(n.ssid, n.security);
                                    else setSelected(selected === n.ssid ? null : n.ssid);
                                }}
                            >
                                Join
                            </button>
                        </span>
                    </div>
                    {selected === n.ssid && !isOpen(n.security) && (
                        <form
                            className="row"
                            onSubmit={(e) => {
                                e.preventDefault();
                                void join(n.ssid, n.security);
                            }}
                        >
                            <input
                                type="password"
                                placeholder={`Password for ${n.ssid}`}
                                value={password}
                                onChange={(e) => setPassword(e.target.value)}
                                autoFocus
                                style={{ flex: 1 }}
                            />
                            <button className="primary" type="submit" disabled={busy || !password}>
                                Connect
                            </button>
                        </form>
                    )}
                </div>
            ))}
            {networks.length === 0 && !scanning && (
                <div className="dim">No networks found — try Rescan, or add one with “Other…”.</div>
            )}
        </div>
    );
}
