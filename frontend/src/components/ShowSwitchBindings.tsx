// Gear-menu panel: bind a key/pad or a Program Change to LOAD A SHOW.
//
// Device-global, which is why it lives in Settings rather than the show editor:
// a show name means something across the whole box. The per-SCENE selectors are
// the opposite case -- a scene id only means something inside its own show, so
// those live on the scene. The two are deliberately separate mechanisms.
import { useEffect, useRef, useState } from "react";

import { api } from "../api/client";
import type { ShowBinding } from "../api/types";
import { useTelemetry, useTelemetryStore } from "../state/telemetryStore";

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = (n: number) => `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;

const describe = (t: ShowBinding["trigger"]) =>
    t.type === "note" ? `${noteName(t.number)} (${t.number})` : `PC ${t.number}`;

export default function ShowSwitchBindings({
    bindings,
    onChange,
}: {
    bindings: ShowBinding[];
    onChange: (next: ShowBinding[]) => void;
}) {
    const [shows, setShows] = useState<string[] | null>(null);
    const [armedFor, setArmedFor] = useState<string | null>(null);
    const armedAt = useRef(0);
    useTelemetry();
    const connected = useTelemetryStore((s) => s.connected);

    useEffect(() => {
        api.listShows().then((r) => setShows(r.shows)).catch(() => setShows([]));
    }, []);

    const bindingFor = (show: string) => bindings.find((b) => b.show === show) ?? null;

    const setBinding = (show: string, trigger: ShowBinding["trigger"] | null) => {
        // A trigger must be unique across shows: the renderer takes the first
        // match, so a duplicate would make the later show unreachable. Drop it
        // from any other show rather than silently shadowing.
        let next = bindings.filter((b) => b.show !== show);
        if (trigger) {
            next = next.filter(
                (b) => !(b.trigger.type === trigger.type && b.trigger.number === trigger.number),
            );
            next.push({ show, trigger });
        }
        onChange(next);
    };

    // Learn: same imperative store subscription the param and scene bindings
    // use, so it never re-subscribes on a telemetry frame.
    useEffect(() => {
        if (!armedFor) return;
        let done = false;
        const tryBind = (latest: ReturnType<typeof useTelemetryStore.getState>["latest"]) => {
            const lc = latest?.lastControl;
            // Only notes arrive on this feed, and only events STRICTLY NEWER
            // than the arming moment may bind. lastControl persists the last
            // thing played, so without the timestamp check arming Learn would
            // instantly re-bind the note used for the PREVIOUS show -- and the
            // uniqueness rule would then steal it from that show, which is
            // exactly how this showed up: the first button reverted to "Learn".
            if (done || !lc || lc.kind !== "note" || lc.ts <= armedAt.current) return;
            done = true;
            const show = armedFor;
            setArmedFor(null);
            setBinding(show, { type: "note", number: lc.number });
        };
        tryBind(useTelemetryStore.getState().latest);
        const unsub = useTelemetryStore.subscribe((s) => tryBind(s.latest));
        const t = setTimeout(() => setArmedFor(null), 15000);   // never listen forever
        return () => { unsub(); clearTimeout(t); };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [armedFor, bindings]);

    if (!shows) return null;

    return (
        <section style={{ display: "flex", flexDirection: "column", gap: 6 }}>
            <strong>Switch shows from MIDI</strong>
            <div className="dim" style={{ fontSize: 12 }}>
                Load a whole show from a key/pad or a Program Change. Scene-level triggers live in the
                scene editor; these are device-wide and work whatever show is open.
            </div>
            {shows.length === 0 && <div className="dim" style={{ fontSize: 12 }}>No shows yet.</div>}
            {shows.map((show) => {
                const b = bindingFor(show);
                const armed = armedFor === show;
                return (
                    <div key={show} className="row" style={{ gap: 8, alignItems: "center" }}>
                        <span style={{ flex: 1 }}>{show}</span>
                        <button
                            className="icon"
                            style={{
                                minWidth: 78,
                                borderColor: armed ? "var(--warn)" : b ? "var(--accent)" : undefined,
                                color: armed ? "var(--warn)" : b ? "var(--accent)" : "var(--text-dim)",
                            }}
                            title="Learn a key or pad for this show"
                            onClick={() => {
                                if (armed) { setArmedFor(null); return; }
                                // Freshest timestamp straight from the store, in the
                                // renderer's own clock -- Date.now() is a different
                                // clock entirely and can never be compared to lc.ts.
                                armedAt.current =
                                    useTelemetryStore.getState().latest?.lastControl?.ts ?? 0;
                                setArmedFor(show);
                            }}
                        >
                            {armed ? (connected ? "listen…" : "no WS!") : b ? describe(b.trigger) : "Learn"}
                        </button>
                        <input
                            type="number"
                            min={0}
                            max={127}
                            step={1}
                            style={{ width: 66 }}
                            title="Program Change number for this show"
                            placeholder="PC"
                            value={b?.trigger.type === "program" ? b.trigger.number : ""}
                            onChange={(e) => {
                                const v = parseInt(e.target.value, 10);
                                setBinding(show, Number.isFinite(v) && v >= 0 && v <= 127
                                    ? { type: "program", number: v } : null);
                            }}
                        />
                        {b && (
                            <button className="icon danger" title="Remove this binding"
                                onClick={() => setBinding(show, null)}>✕</button>
                        )}
                    </div>
                );
            })}
        </section>
    );
}
