import { useState } from "react";
import { useNavigate } from "react-router-dom";

import { api } from "../api/client";

export default function Login() {
    const [password, setPassword] = useState("");
    const [error, setError] = useState<string | null>(null);
    const navigate = useNavigate();

    const submit = async (e: React.FormEvent) => {
        e.preventDefault();
        try {
            await api.login(password);
            navigate("/edit");
        } catch {
            setError("Wrong password");
        }
    };

    return (
        <div className="page" style={{ maxWidth: 360, marginTop: "18vh" }}>
            <form className="card" onSubmit={submit} style={{ display: "flex", flexDirection: "column", gap: 12 }}>
                <h2>LivePi Videosynth</h2>
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
