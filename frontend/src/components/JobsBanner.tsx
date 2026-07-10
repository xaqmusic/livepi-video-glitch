// Global background-activity strip under the topbar: any clip still
// queued/transcoding shows here with live progress no matter which screen
// you're on (transcodes on the Pi take minutes -- without this, leaving
// the Clips screen made them invisible). Errors stick until dismissed.

import { useEffect, useRef, useState } from "react";

import { api } from "../api/client";
import type { JobSummary } from "../api/types";

const ACTIVE_STATES = ["queued", "probing", "transcoding"];

export default function JobsBanner() {
    const [jobs, setJobs] = useState<JobSummary[]>([]);
    const [dismissed, setDismissed] = useState<Set<string>>(new Set());
    const timer = useRef<ReturnType<typeof setInterval> | null>(null);

    useEffect(() => {
        const tick = async () => {
            if (document.hidden) return;
            try {
                const { jobs } = await api.listJobs();
                setJobs(jobs);
            } catch {
                /* backend momentarily away -- keep last state */
            }
        };
        tick();
        timer.current = setInterval(tick, 3000);
        return () => {
            if (timer.current) clearInterval(timer.current);
        };
    }, []);

    const active = jobs.filter((j) => ACTIVE_STATES.includes(j.state));
    const errors = jobs.filter((j) => j.state === "error" && !dismissed.has(j.id));
    if (active.length === 0 && errors.length === 0) return null;

    return (
        <div style={{ display: "flex", flexDirection: "column", gap: 2 }}>
            {active.map((j) => (
                <div key={j.id} className="jobs-banner">
                    <span>
                        ⚙ {j.state === "transcoding" ? "Transcoding" : "Preparing"} <strong>{j.name}</strong>
                        {j.state === "transcoding" && ` — ${Math.round(j.progress * 100)}%`}
                    </span>
                    <div className="jobs-banner-track">
                        <div className="jobs-banner-fill" style={{ width: `${Math.round(j.progress * 100)}%` }} />
                    </div>
                </div>
            ))}
            {errors.map((j) => (
                <div key={j.id} className="jobs-banner error">
                    <span>
                        ✕ <strong>{j.name}</strong> failed: {j.error}
                    </span>
                    <button className="icon" onClick={() => setDismissed(new Set(dismissed).add(j.id))}>✕</button>
                </div>
            ))}
        </div>
    );
}
