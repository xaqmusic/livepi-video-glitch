// Entry point: saved setlists, which one the Pi boots into, duplicate/new.

import { useCallback, useEffect, useState } from "react";
import { Link, useNavigate } from "react-router-dom";

import { api, ApiError } from "../api/client";

export default function ShowLibrary() {
    const [shows, setShows] = useState<string[]>([]);
    const [active, setActive] = useState<string | null>(null);
    const [error, setError] = useState<string | null>(null);
    const navigate = useNavigate();

    const refresh = useCallback(async () => {
        try {
            const result = await api.listShows();
            setShows(result.shows);
            setActive(result.active);
        } catch (e) {
            if (e instanceof ApiError && e.status === 401) navigate("/login");
            else setError(String(e));
        }
    }, [navigate]);

    useEffect(() => {
        void refresh();
    }, [refresh]);

    const createShow = async (copyFrom?: string) => {
        const name = prompt(copyFrom ? `Duplicate "${copyFrom}" as:` : "New show name:");
        if (!name) return;
        try {
            await api.createShow(name.trim().replace(/\s+/g, "-").toLowerCase(), copyFrom);
            await refresh();
        } catch (e) {
            alert(String(e));
        }
    };

    return (
        <div className="page">
            <div className="row" style={{ justifyContent: "space-between" }}>
                <h2 style={{ margin: 0 }}>Shows</h2>
                <button className="primary" onClick={() => void createShow()}>
                    New show
                </button>
            </div>
            {error && <div className="error">{error}</div>}
            {shows.map((name) => (
                <div key={name} className="card row" style={{ justifyContent: "space-between" }}>
                    <div className="row">
                        <Link to={`/edit/${name}`} style={{ color: "var(--text)", fontWeight: 600, textDecoration: "none", fontSize: 15 }}>
                            {name}
                        </Link>
                        {active === name && <span style={{ color: "var(--ok)", fontSize: 12 }}>● active on the box</span>}
                    </div>
                    <div className="row">
                        {active !== name && (
                            <button
                                onClick={() => void api.setActiveShow(name).then(refresh).catch((e) => alert(String(e)))}
                            >
                                Make active
                            </button>
                        )}
                        <button onClick={() => void createShow(name)}>Duplicate</button>
                        <button
                            className="danger"
                            disabled={active === name}
                            title={active === name ? "Switch the active show first" : undefined}
                            onClick={() => {
                                if (confirm(`Delete show "${name}"?`))
                                    void api.deleteShow(name).then(refresh).catch((e) => alert(String(e)));
                            }}
                        >
                            Delete
                        </button>
                    </div>
                </div>
            ))}
            {shows.length === 0 && <div className="dim">No shows yet -- create one to start building a set.</div>}
        </div>
    );
}
