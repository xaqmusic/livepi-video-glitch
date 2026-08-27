import { useState } from "react";
import { useNavigate } from "react-router-dom";

import { api } from "../api/client";
import { BRAND_MARK, BRAND_NAME } from "../brand";

export default function Login() {
    const [password, setPassword] = useState("");
    const [error, setError] = useState<string | null>(null);
    const navigate = useNavigate();

    const submit = async (e: React.FormEvent) => {
        e.preventDefault();
        try {
            await api.login(password);
            // Fresh box still on its printed password? Carry a first-run flag
            // to /edit so App pops the change-password dialog.
            const { mustChangePassword } = await api.authStatus();
            navigate("/edit", mustChangePassword ? { state: { firstRun: true } } : undefined);
        } catch {
            setError("Wrong password");
        }
    };

    return (
        <div className="page" style={{ maxWidth: 360, marginTop: "18vh" }}>
            <form className="card" onSubmit={submit} style={{ display: "flex", flexDirection: "column", gap: 12 }}>
                <h2 style={{ margin: 0 }}>
                    <span aria-hidden="true" style={{ marginRight: 8 }}>{BRAND_MARK}</span>
                    {BRAND_NAME} Videosynth
                </h2>
                <input
                    type="password"
                    placeholder="Password"
                    value={password}
                    onChange={(e) => setPassword(e.target.value)}
                    autoFocus
                />
                {error && <div className="error">{error}</div>}
                <button className="primary" type="submit">
                    Log in
                </button>
            </form>
        </div>
    );
}
