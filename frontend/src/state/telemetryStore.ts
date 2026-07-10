// ONE shared telemetry WebSocket, refcounted: connects when the first
// consumer mounts (a MappableControl arming Learn, the MappingsTab's
// heaviness readout, Live mode) and closes when the last one leaves --
// the backend only reads the renderer's status file while a client is
// connected, so idle cost stays zero end to end.

import { useEffect } from "react";
import { create } from "zustand";

import type { Telemetry } from "../api/types";

interface TelemetryState {
    latest: Telemetry | null;
    connected: boolean;
    refs: number;
    acquire: () => void;
    release: () => void;
}

let ws: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;

export const useTelemetryStore = create<TelemetryState>((set, get) => ({
    latest: null,
    connected: false,
    refs: 0,

    acquire: () => {
        const refs = get().refs + 1;
        set({ refs });
        if (refs === 1 && !ws) {
            connect(set, get);
        }
    },

    release: () => {
        const refs = Math.max(0, get().refs - 1);
        set({ refs });
        if (refs === 0) {
            if (reconnectTimer) {
                clearTimeout(reconnectTimer);
                reconnectTimer = null;
            }
            if (ws) {
                ws.close();
                ws = null;
                set({ connected: false });
            }
        }
    },
}));

type Set = (partial: Partial<TelemetryState>) => void;
type Get = () => TelemetryState;

function connect(set: Set, get: Get) {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    ws = new WebSocket(`${proto}//${location.host}/ws/telemetry`);
    ws.onopen = () => set({ connected: true });
    ws.onmessage = (ev) => set({ latest: JSON.parse(ev.data) as Telemetry });
    ws.onclose = () => {
        set({ connected: false });
        ws = null;
        // Auto-reconnect while anyone still wants telemetry -- a dropped
        // socket (backend restart, network blip) used to leave Learn stuck
        // at "no WS!" until a page reload.
        if (get().refs > 0 && !reconnectTimer) {
            reconnectTimer = setTimeout(() => {
                reconnectTimer = null;
                if (get().refs > 0 && !ws) connect(set, get);
            }, 2000);
        }
    };
}

/** Mount-scoped subscription to live telemetry. */
export function useTelemetry(): Telemetry | null {
    // Per-field selectors, NOT a whole-store subscription: a scene screen
    // holds dozens of MappableControls, and subscribing them all to every
    // store write (including acquire/release's refs bookkeeping) multiplies
    // renders across the board -- the churn behind the "maximum update
    // depth" crash hunt. latest is the only field a consumer renders from;
    // acquire/release are stable references from create().
    const latest = useTelemetryStore((s) => s.latest);
    const acquire = useTelemetryStore((s) => s.acquire);
    const release = useTelemetryStore((s) => s.release);
    useEffect(() => {
        acquire();
        return release;
    }, [acquire, release]);
    return latest;
}
