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

export const useTelemetryStore = create<TelemetryState>((set, get) => ({
    latest: null,
    connected: false,
    refs: 0,

    acquire: () => {
        const refs = get().refs + 1;
        set({ refs });
        if (refs === 1 && !ws) {
            const proto = location.protocol === "https:" ? "wss:" : "ws:";
            ws = new WebSocket(`${proto}//${location.host}/ws/telemetry`);
            ws.onopen = () => set({ connected: true });
            ws.onmessage = (ev) => set({ latest: JSON.parse(ev.data) as Telemetry });
            ws.onclose = () => {
                set({ connected: false });
                ws = null;
            };
        }
    },

    release: () => {
        const refs = Math.max(0, get().refs - 1);
        set({ refs });
        if (refs === 0 && ws) {
            ws.close();
            ws = null;
            set({ connected: false });
        }
    },
}));

/** Mount-scoped subscription to live telemetry. */
export function useTelemetry(): Telemetry | null {
    const { latest, acquire, release } = useTelemetryStore();
    useEffect(() => {
        acquire();
        return release;
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);
    return latest;
}
