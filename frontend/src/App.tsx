import { useState } from "react";
import { Navigate, NavLink, Route, Routes, useLocation } from "react-router-dom";

import JobsBanner from "./components/JobsBanner";
import PasswordDialog from "./components/PasswordDialog";
import ClipLibrary from "./screens/ClipLibrary";
import LiveMode from "./screens/LiveMode";
import Login from "./screens/Login";
import SceneEditor from "./screens/SceneEditor";
import SetlistEditor from "./screens/SetlistEditor";
import ShowLibrary from "./screens/ShowLibrary";

// Edit mode (laptop/tablet) and Live mode (phone) are deliberately separate
// routes with separate layouts, not one responsive view -- see
// docs/videosynth-frontend.md "Two modes, not one responsive view".
export default function App() {
    const location = useLocation();
    const isLive = location.pathname.startsWith("/live");
    const isLogin = location.pathname.startsWith("/login");
    const [showPassword, setShowPassword] = useState(false);

    return (
        <>
            {!isLive && !isLogin && (
                <nav className="topbar">
                    <strong>LivePi</strong>
                    <NavLink to="/edit">Shows</NavLink>
                    <NavLink to="/clips">Clips</NavLink>
                    <NavLink to="/live">Live</NavLink>
                    <button className="icon" style={{ marginLeft: "auto" }} title="Change password"
                        onClick={() => setShowPassword(true)}>🔑</button>
                </nav>
            )}
            {!isLive && !isLogin && <JobsBanner />}
            {showPassword && <PasswordDialog onClose={() => setShowPassword(false)} />}
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
