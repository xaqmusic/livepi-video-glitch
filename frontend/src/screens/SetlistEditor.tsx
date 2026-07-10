// The ordered scene list Click cycles through live. Up/down buttons are
// the PRIMARY reorder control (touch-friendly; drag-and-drop is a possible
// later desktop bonus, never the only way -- docs/videosynth-frontend.md).

import { useEffect, useRef } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";

import { api, downloadJson, newId, readJsonFile } from "../api/client";
import { useShowStore } from "../state/showStore";
import SaveStatus from "../components/SaveStatus";

export default function SetlistEditor() {
    const { show: showName } = useParams<{ show: string }>();
    const { show, open, edit } = useShowStore();
    const navigate = useNavigate();
    // Must be declared with the other hooks, ABOVE the early `!show` return --
    // hooks can't sit behind a conditional or the count changes between the
    // loading and loaded renders (Rules of Hooks).
    const fileInput = useRef<HTMLInputElement>(null);

    useEffect(() => {
        if (showName) {
            open(showName).catch((e) => {
                alert(String(e));
                navigate("/edit");
            });
        }
    }, [showName, open, navigate]);

    if (!show) return <div className="page dim">Loading…</div>;

    const move = (index: number, delta: number) => {
        edit((draft) => {
            const target = index + delta;
            if (target < 0 || target >= draft.scenes.length) return;
            const [scene] = draft.scenes.splice(index, 1);
            draft.scenes.splice(target, 0, scene);
        });
    };

    const addScene = () => {
        const name = prompt("Scene name:");
        if (!name) return;
        edit((draft) => {
            draft.scenes.push({
                id: newId("scene"),
                name,
                layers: [],
                mappings: [],
                postEffects: {},
            });
        });
    };

    const renameScene = (index: number, current: string) => {
        const name = prompt("Scene name:", current);
        if (!name || name === current) return;
        edit((draft) => {
            draft.scenes[index].name = name;
        });
    };

    const exportScene = (index: number) => {
        const scene = show!.scenes[index];
        const stem = (scene.name || "scene").replace(/[^\w-]+/g, "_");
        downloadJson(`${stem}.lpscene`, scene);
    };

    const importSceneFile = async (file: File) => {
        if (!showName) return;
        try {
            const scene = await readJsonFile(file);
            // Persist any pending draft edit, import server-side (fresh ids +
            // lenient clip check), then reload the draft with the new scene.
            await useShowStore.getState().saveNow();
            const res = await api.importScene(showName, scene);
            if (res.warnings?.length) alert("Imported with warnings:\n\n" + res.warnings.join("\n"));
            await open(showName);
        } catch (e) {
            alert(String(e));
        }
    };

    const duplicateScene = (index: number) => {
        edit((draft) => {
            const copy = structuredClone(draft.scenes[index]);
            copy.id = newId("scene");
            copy.name = `${copy.name} copy`;
            // Fresh layer ids so mappings inside the copy re-point correctly.
            const idMap = new Map<string, string>();
            for (const layer of copy.layers) {
                const fresh = newId("layer");
                idMap.set(layer.id, fresh);
                layer.id = fresh;
            }
            for (const mapping of copy.mappings)
                for (const target of mapping.targets)
                    if (target.layerId) target.layerId = idMap.get(target.layerId) ?? target.layerId;
            draft.scenes.splice(index + 1, 0, copy);
        });
    };

    return (
        <div className="page">
            <div className="row" style={{ justifyContent: "space-between" }}>
                <h2 style={{ margin: 0 }}>
                    <Link to="/edit" style={{ color: "var(--text-dim)", textDecoration: "none" }}>Shows</Link>
                    {" / "}{showName}
                </h2>
                <div className="row">
                    <SaveStatus />
                    <input
                        ref={fileInput}
                        type="file"
                        accept=".lpscene,.json"
                        style={{ display: "none" }}
                        onChange={(e) => {
                            const f = e.target.files?.[0];
                            if (f) void importSceneFile(f);
                            e.target.value = "";
                        }}
                    />
                    <button onClick={() => fileInput.current?.click()}>Import scene</button>
                    <button className="primary" onClick={addScene}>Add scene</button>
                </div>
            </div>
            {show.scenes.map((scene, i) => (
                <div key={scene.id} className="card row" style={{ justifyContent: "space-between" }}>
                    <div className="row">
                        <span className="dim" style={{ minWidth: 24 }}>{i + 1}.</span>
                        <Link
                            to={`/edit/${showName}/scene/${scene.id}`}
                            style={{ color: "var(--text)", fontWeight: 600, textDecoration: "none", fontSize: 15 }}
                        >
                            {scene.name}
                        </Link>
                        <span className="dim">
                            {scene.layers.length} layer{scene.layers.length === 1 ? "" : "s"}, {scene.mappings.length} mapping{scene.mappings.length === 1 ? "" : "s"}
                        </span>
                    </div>
                    <div className="row">
                        <button className="primary" onClick={() => navigate(`/edit/${showName}/scene/${scene.id}`)}>
                            Edit
                        </button>
                        <button className="icon" disabled={i === 0} onClick={() => move(i, -1)}>▲</button>
                        <button className="icon" disabled={i === show.scenes.length - 1} onClick={() => move(i, 1)}>▼</button>
                        <button onClick={() => renameScene(i, scene.name)}>Rename</button>
                        <button onClick={() => exportScene(i)} title="Download as a .lpscene file">Export</button>
                        <button onClick={() => duplicateScene(i)}>Duplicate</button>
                        <button
                            className="danger"
                            onClick={() => {
                                if (confirm(`Delete scene "${scene.name}"?`))
                                    edit((draft) => { draft.scenes.splice(i, 1); });
                            }}
                        >
                            Delete
                        </button>
                    </div>
                </div>
            ))}
            {show.scenes.length === 0 && <div className="dim">No scenes -- add one to start.</div>}
        </div>
    );
}
