import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// Dev proxies /api and /ws to the backend (scripts/run-backend-dev.sh on
// :8080) so cookies and the telemetry WebSocket work identically in dev
// and in the built app the backend serves itself.
export default defineConfig({
    plugins: [react()],
    server: {
        proxy: {
            "/api": "http://localhost:8080",
            "/ws": { target: "ws://localhost:8080", ws: true },
        },
    },
});
