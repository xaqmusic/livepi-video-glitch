import { useEffect, useState } from "react";
import { Navigate, NavLink, Route, Routes, useLocation } from "react-router-dom";

import JobsBanner from "./components/JobsBanner";
import PasswordDialog from "./components/PasswordDialog";
import SettingsDialog from "./components/SettingsDialog";
import ClipLibrary from "./screens/ClipLibrary";
import LiveMode from "./screens/LiveMode";
import Login from "./screens/Login";
import Network from "./screens/Network";
import SceneEditor from "./screens/SceneEditor";
import SetlistEditor from "./screens/SetlistEditor";
import ShowLibrary from "./screens/ShowLibrary";
import { useShowStore } from "./state/showStore";

// Edit mode (laptop/tablet) and Live mode (phone) are deliberately separate
// routes with separate layouts, not one responsive view -- see
// docs/videosynth-frontend.md "Two modes, not one responsive view".
export default function App() {
    const location = useLocation();
    const isLive = location.pathname.startsWith("/live");
    const isLogin = location.pathname.startsWith("/login");
    // null = closed; {firstRun} tracks whether it opened from first login
    // (printed factory password) vs the manual 🔑 button, so the copy differs.
    const [pwDialog, setPwDialog] = useState<{ firstRun: boolean } | null>(null);
    const firstRun = Boolean((location.state as { firstRun?: boolean } | null)?.firstRun);
    // First login on the printed password: NUDGE to set a private one -- don't
    // force a modal (you couldn't even see the code to re-type once the setup
    // card closed). While no private password is set, the change dialog asks
    // only for the new one (the session already proves the printed code).
    const [mustChange, setMustChange] = useState(firstRun);
    const [nudgeDismissed, setNudgeDismissed] = useState(false);
    const [settingsOpen, setSettingsOpen] = useState(false);
    useEffect(() => {
        if (firstRun) setMustChange(true);
    }, [firstRun]);
    // The show currently open in the editor -- lets the top bar jump straight
    // to its scene list from anywhere.
    const openShow = useShowStore((s) => s.name);

    return (
        <>
            {!isLive && !isLogin && (
                <nav className="topbar">
                    <strong>LivePi</strong>
                    <NavLink to="/edit">Shows</NavLink>
                    {openShow && <NavLink to={`/edit/${encodeURIComponent(openShow)}`} end>Scenes</NavLink>}
                    <NavLink to="/clips">Clips</NavLink>
                    <NavLink to="/network">Network</NavLink>
                    <NavLink to="/live">Live</NavLink>
                    <button className="icon" style={{ marginLeft: "auto" }} title="Settings"
                        onClick={() => setSettingsOpen(true)}>⚙️</button>
                </nav>
            )}
            {!isLive && !isLogin && mustChange && !nudgeDismissed && (
                <div className="row" style={{ padding: "8px 16px", background: "#2b2b1c", gap: 12, alignItems: "center" }}>
                    <span>This box is using its printed password — set a private one.</span>
                    <button className="primary" onClick={() => { setPwDialog({ firstRun: true }); setNudgeDismissed(true); }}>
                        Set password
                    </button>
                    <button onClick={() => setNudgeDismissed(true)}>Later</button>
                </div>
            )}
            {!isLive && !isLogin && <JobsBanner />}
            {pwDialog && (
                <PasswordDialog
                    firstRun={pwDialog.firstRun}
                    onChanged={() => setMustChange(false)}
                    onClose={() => setPwDialog(null)}
                />
            )}
            {settingsOpen && (
                <SettingsDialog
                    firstRunPassword={mustChange}
                    onPasswordChanged={() => setMustChange(false)}
                    onClose={() => setSettingsOpen(false)}
                />
            )}
            <Routes>
                <Route path="/login" element={<Login />} />
                <Route path="/edit" element={<ShowLibrary />} />
                <Route path="/edit/:show" element={<SetlistEditor />} />
                <Route path="/edit/:show/scene/:sceneId" element={<SceneEditor />} />
                <Route path="/clips" element={<ClipLibrary />} />
                <Route path="/network" element={<Network />} />
                <Route path="/live" element={<LiveMode />} />
                <Route path="*" element={<Navigate to="/edit" replace />} />
            </Routes>
        </>
    );
}
