// Shared clip grid -- any layer in any scene can reference any clip here.

import { useCallback, useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";

import { api, ApiError } from "../api/client";
import type { Clip } from "../api/types";
import UploadDropzone from "../components/UploadDropzone";

export default function ClipLibrary() {
    const [clips, setClips] = useState<Clip[]>([]);
    const navigate = useNavigate();

    const refresh = useCallback(async () => {
        try {
            setClips((await api.listClips()).clips);
        } catch (e) {
            if (e instanceof ApiError && e.status === 401) navigate("/login");
        }
    }, [navigate]);

    useEffect(() => {
        void refresh();
    }, [refresh]);

    return (
        <div className="page">
            <h2>Clip library</h2>
            <UploadDropzone onUploaded={() => void refresh()} />
            <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fill, minmax(200px, 1fr))", gap: 12 }}>
                {clips.map((clip) => (
                    <div key={clip.id} className="card" style={{ padding: 10, display: "flex", flexDirection: "column", gap: 6 }}>
                        {clip.thumbUrl ? (
                            <img src={clip.thumbUrl} alt="" style={{ width: "100%", aspectRatio: "16/9", objectFit: "cover", borderRadius: 4 }} />
                        ) : (
                            <div className="dim" style={{ width: "100%", aspectRatio: "16/9", background: "var(--bg-input)", borderRadius: 4, display: "grid", placeItems: "center" }}>
                                no thumb
                            </div>
                        )}
                        <div style={{ fontWeight: 600, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
                            {clip.name ?? clip.path}
                        </div>
                        <div className="dim" style={{ fontSize: 12 }}>
                            {clip.height ? `${clip.height}p` : "?"} · {clip.duration ? `${Math.round(clip.duration)}s` : "?"}
                            {clip.exists === false && <span className="error"> · file missing!</span>}
                        </div>
                        <button
                            className="danger"
                            onClick={() => {
                                if (confirm(`Delete "${clip.name ?? clip.path}"?`))
                                    void api.deleteClip(clip.id).then(refresh).catch((e) => alert(String(e)));
                            }}
                        >
                            Delete
                        </button>
                    </div>
                ))}
            </div>
            {clips.length === 0 && <div className="dim">No clips yet -- drop some footage above.</div>}
        </div>
    );
}
