#!/usr/bin/env bash
# Convenience wrapper for desktop dev: builds, then runs BOTH the renderer and
# the backend (web editor + Live mode on :8080) together, the way the Pi runs
# them as a pair. Ctrl-C, a SIGTERM to this script, or closing the renderer
# window stops both cleanly. The checked-in config/app.json defaults
# control_source to "mock", so this Just Works with no hardware attached --
# copy bin/data/config/app.local.example.json to app.local.json to point it at
# a real MIDI device or a different scene list instead. Extra args are passed
# through to `make` (e.g. `./run.sh Debug`).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

make "$@"

# Backend venv (created on first run, same recipe as run-backend-dev.sh).
VENV="$REPO_DIR/backend/.venv"
if [ ! -x "$VENV/bin/uvicorn" ]; then
    echo "Setting up backend venv..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q -r "$REPO_DIR/backend/requirements.txt"
fi

BACKEND_PID=""
RENDER_PID=""
# Disarm the traps first so a second signal can't re-enter, then take both
# children down (they each shut down cleanly on SIGTERM) and reap them.
cleanup() {
    trap - INT TERM EXIT
    [ -n "$RENDER_PID" ] && kill "$RENDER_PID" 2>/dev/null || true
    [ -n "$BACKEND_PID" ] && kill "$BACKEND_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup INT TERM EXIT

# Backend on :8080, serving the built frontend and sharing bin/data +
# /tmp/livepi with the renderer below (so the web UI drives this window).
"$VENV/bin/uvicorn" livepi_backend.main:app \
    --app-dir "$REPO_DIR/backend" --host 0.0.0.0 --port "${LIVEPI_PORT:-8080}" &
BACKEND_PID=$!

# Renderer (the GL window). Backgrounded too so a SIGTERM to THIS script can
# stop both; we wait on it as the primary process, and its exit (or a trapped
# signal) triggers cleanup of the backend.
./bin/livepi-video-glitch &
RENDER_PID=$!

wait "$RENDER_PID"
