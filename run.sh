#!/usr/bin/env bash
# Convenience wrapper for desktop dev: builds and runs the app. The
# checked-in config/app.json defaults control_source to "mock", so this Just
# Works with no hardware attached -- copy bin/data/config/app.local.example.json
# to app.local.json to point it at a real MIDI device or a different scene
# list instead. Extra args are passed through to `make` (e.g. `./run.sh Debug`).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

make "$@"
./bin/livepi-video-glitch
