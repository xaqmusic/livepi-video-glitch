// One combined dropzone + Browse button (docs/videosynth-frontend.md):
// drag-and-drop for the desktop file-browser case, an explicit button for
// phone/tablet where that gesture doesn't exist. Same upload path either
// way; transcode progress polled from the job endpoint.

import { useRef, useState } from "react";

import { api } from "../api/client";
import type { Clip } from "../api/types";

export default function UploadDropzone({ onUploaded }: { onUploaded: (clip: Clip) => void }) {
    const [dragOver, setDragOver] = useState(false);
    const [status, setStatus] = useState<string | null>(null);
    const [progress, setProgress] = useState<number | null>(null);
    const fileInput = useRef<HTMLInputElement>(null);

    const upload = async (file: File) => {
        setStatus(`Uploading ${file.name}…`);
        setProgress(null);
        try {
            const { jobId } = await api.uploadClip(file);
            for (;;) {
                const job = await api.jobStatus(jobId);
                if (job.state === "done" && job.clip) {
                    setStatus(null);
                    setProgress(null);
                    onUploaded(job.clip);
                    return;
                }
                if (job.state === "error") {
                    setStatus(`Failed: ${job.error}`);
                    setProgress(null);
                    return;
                }
                setStatus(job.state === "transcoding" ? "Transcoding for the Pi…" : "Processing…");
                setProgress(job.state === "transcoding" ? job.progress : null);
                await new Promise((r) => setTimeout(r, 500));
            }
        } catch (e) {
            setStatus(`Failed: ${e instanceof Error ? e.message : e}`);
        }
    };

    return (
        <div
            className="card"
            style={{
                textAlign: "center",
                borderStyle: "dashed",
                borderColor: dragOver ? "var(--accent)" : "var(--border-strong)",
                padding: 24,
            }}
            onDragOver={(e) => { e.preventDefault(); setDragOver(true); }}
            onDragLeave={() => setDragOver(false)}
            onDrop={(e) => {
                e.preventDefault();
                setDragOver(false);
                const file = e.dataTransfer.files[0];
                if (file) void upload(file);
            }}
        >
            {status ? (
                <div>
                    <div>{status}</div>
                    {progress !== null && (
                        <div style={{ background: "var(--bg-input)", borderRadius: 4, marginTop: 8, height: 8 }}>
                            <div style={{ width: `${Math.round(progress * 100)}%`, background: "var(--accent)", height: "100%", borderRadius: 4 }} />
                        </div>
                    )}
                </div>
            ) : (
                <div className="dim">
                    Drop a video here, or{" "}
                    <button onClick={() => fileInput.current?.click()}>Browse…</button>
                    <div style={{ fontSize: 12, marginTop: 6 }}>
                        Anything a phone or camera produces works -- non-H.264 files get transcoded automatically.
                    </div>
                </div>
            )}
            <input
                ref={fileInput}
                type="file"
                accept="video/*"
                hidden
                onChange={(e) => {
                    const file = e.target.files?.[0];
                    if (file) void upload(file);
                    e.target.value = "";
                }}
            />
        </div>
    );
}
