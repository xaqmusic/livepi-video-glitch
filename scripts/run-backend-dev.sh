#!/usr/bin/env bash
# Desktop dev: run the backend with auto-reload against the repo's own
# bin/data -- the renderer (./run.sh) and the backend share the same files
# exactly as they do on the Pi. First run creates the venv.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="$REPO_DIR/backend/.venv"

if [ ! -d "$VENV" ]; then
    echo "Creating backend venv..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q -r "$REPO_DIR/backend/requirements.txt"
fi

cd "$REPO_DIR/backend"
exec "$VENV/bin/uvicorn" livepi_backend.main:app --reload --host 0.0.0.0 --port "${LIVEPI_PORT:-8080}"
