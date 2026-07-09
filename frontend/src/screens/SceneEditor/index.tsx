// The core screen (docs/videosynth-frontend.md): layer stack (top =
// foreground), scene-wide post effects, and the mappings audit tab.

import { useEffect, useState } from "react";
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

    useEffect(() => {
        api.getEffects().then(setManifest).catch(() => navigate("/login"));
        api.listClips().then((r) => setClips(r.clips)).catch(() => {});
    }, [navigate]);

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
                    <LayerStack scene={scene} manifest={manifest} clips={clips} />
                    <PostEffectsPanel scene={scene} manifest={manifest} />
                </>
            ) : (
                <MappingsTab scene={scene} manifest={manifest} />
            )}
        </div>
    );
}
