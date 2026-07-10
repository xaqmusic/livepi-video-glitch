import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// Dev proxies /api and /ws to the backend (scripts/run-backend-dev.sh on
// :8080) so cookies and the telemetry WebSocket work identically in dev
// and in the built app the backend serves itself.
export default defineConfig({
    plugins: [react()],
    // keepNames + sourcemaps: minified for size, but component/function
    // names survive so ErrorBoundary's component stack stays readable if
    // anything ever crashes at the rig again (the React #185 hunt needed
    // a whole unminified redeploy to get a usable stack -- never again).
    esbuild: { keepNames: true },
    build: { sourcemap: true },
    server: {
        proxy: {
            "/api": "http://localhost:8080",
            "/ws": { target: "ws://localhost:8080", ws: true },
        },
    },
});
