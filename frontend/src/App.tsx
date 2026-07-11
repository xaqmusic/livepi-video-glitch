import { useEffect, useState } from "react";
import { Navigate, NavLink, Route, Routes, useLocation } from "react-router-dom";

import JobsBanner from "./components/JobsBanner";
import PasswordDialog from "./components/PasswordDialog";
import ClipLibrary from "./screens/ClipLibrary";
import LiveMode from "./screens/LiveMode";
import Login from "./screens/Login";
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
    useEffect(() => {
        if (firstRun) setPwDialog({ firstRun: true });
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
                    <NavLink to="/live">Live</NavLink>
                    <button className="icon" style={{ marginLeft: "auto" }} title="Change password"
                        onClick={() => setPwDialog({ firstRun: false })}>🔑</button>
                </nav>
            )}
            {!isLive && !isLogin && <JobsBanner />}
            {pwDialog && <PasswordDialog firstRun={pwDialog.firstRun} onClose={() => setPwDialog(null)} />}
            <Routes>
                <Route path="/login" element={<Login />} />
                <Route path="/edit" element={<ShowLibrary />} />
                <Route path="/edit/:show" element={<SetlistEditor />} />
                <Route path="/edit/:show/scene/:sceneId" element={<SceneEditor />} />
                <Route path="/clips" element={<ClipLibrary />} />
                <Route path="/live" element={<LiveMode />} />
                <Route path="*" element={<Navigate to="/edit" replace />} />
            </Routes>
        </>
    );
}
