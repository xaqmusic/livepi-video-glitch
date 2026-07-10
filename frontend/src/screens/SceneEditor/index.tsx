// The core screen (docs/videosynth-frontend.md): layer stack (top =
// foreground), scene-wide post effects, and the mappings audit tab.

import { useCallback, useEffect, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";

import { api } from "../../api/client";
import type { Clip, EffectsManifest } from "../../api/types";
import SaveStatus from "../../components/SaveStatus";
import { findScene, useShowStore } from "../../state/showStore";
import LayerStack from "./LayerStack";
import MappingsTab from "./MappingsTab";
import PostEffectsPanel from "./PostEffectsPanel";

export default function SceneEditor() {
    const { show: showName, sceneId } = useParams<{ show: string; sceneId: string }>();
    const { show, open } = useShowStore();
    const [manifest, setManifest] = useState<EffectsManifest | null>(null);
    const [clips, setClips] = useState<Clip[]>([]);
    const [tab, setTab] = useState<"layers" | "mappings">("layers");
    const navigate = useNavigate();

    useEffect(() => {
        if (showName && (!show || useShowStore.getState().name !== showName)) {
            open(showName).catch(() => navigate("/edit"));
        }
    }, [showName, show, open, navigate]);

    const refreshClips = useCallback(() => api.listClips().then((r) => setClips(r.clips)).catch(() => {}), []);

    useEffect(() => {
        api.getEffects().then(setManifest).catch(() => navigate("/login"));
        void refreshClips();
    }, [navigate, refreshClips]);

    const scene = show && sceneId ? findScene(show, sceneId) : undefined;
    if (!show || !scene || !manifest) return <div className="page dim">Loading…</div>;

    return (
        <div className="page">
            <div className="row" style={{ justifyContent: "space-between" }}>
                <h2 style={{ margin: 0 }}>
                    <Link to="/edit" style={{ color: "var(--text-dim)", textDecoration: "none" }}>Shows</Link>
                    {" / "}
                    <Link to={`/edit/${showName}`} style={{ color: "var(--text-dim)", textDecoration: "none" }}>{showName}</Link>
                    {" / "}{scene.name}
                </h2>
                <div className="row">
                    <span className="dim" style={{ fontSize: 12 }}>transition in</span>
                    <select
                        value={scene.transition?.style ?? "none"}
                        title="Effect ramp used when switching INTO this scene (masks the decoder spin-up)"
                        onChange={(e) => {
                            const style = e.target.value as "none" | "fade" | "tear" | "shatter";
                            useShowStore.getState().edit((draft) => {
                                const s = draft.scenes.find((x) => x.id === scene.id);
                                if (s) s.transition = { style, duration: s.transition?.duration ?? 0.8 };
                            });
                        }}
                    >
                        <option value="none">none</option>
                        <option value="fade">fade</option>
                        <option value="tear">tear</option>
                        <option value="shatter">shatter</option>
                    </select>
                    {(scene.transition?.style ?? "none") !== "none" && (
                        <input
                            type="number"
                            min={0.1}
                            max={5}
                            step={0.1}
                            style={{ width: 64 }}
                            title="Transition duration, seconds"
                            value={scene.transition?.duration ?? 0.8}
                            onChange={(e) => {
                                const duration = Math.min(5, Math.max(0.1, parseFloat(e.target.value) || 0.8));
                                useShowStore.getState().edit((draft) => {
                                    const s = draft.scenes.find((x) => x.id === scene.id);
                                    if (s?.transition) s.transition.duration = duration;
                                });
                            }}
                        />
                    )}
                    <SaveStatus />
                    <button
                        title="Jump the renderer to this scene"
                        onClick={() => void api.command({ type: "goto", sceneId: scene.id }).catch((e) => alert(String(e)))}
                    >
                        ▶ Preview on box
                    </button>
                </div>
            </div>

            <div className="row">
                <button className={tab === "layers" ? "primary" : ""} onClick={() => setTab("layers")}>
                    Layers & effects
                </button>
                <button className={tab === "mappings" ? "primary" : ""} onClick={() => setTab("mappings")}>
                    Mappings ({scene.mappings.length})
                </button>
            </div>

            {tab === "layers" ? (
                <>
                    <LayerStack scene={scene} manifest={manifest} clips={clips} onClipsChanged={refreshClips} />
                    <PostEffectsPanel scene={scene} manifest={manifest} />
                </>
            ) : (
                <MappingsTab scene={scene} manifest={manifest} />
            )}
        </div>
    );
}
