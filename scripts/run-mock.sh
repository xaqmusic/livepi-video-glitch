#!/usr/bin/env bash
# Convenience wrapper for desktop dev: builds and runs the app. The
# checked-in config/app.json defaults control_source to "mock", so this Just
# Works with no hardware attached. Extra args are passed through to `make`
# (e.g. `./scripts/run-mock.sh Debug`).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_DIR"

make "$@"
./bin/livepi-video-glitch
