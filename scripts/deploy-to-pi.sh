#!/usr/bin/env bash
# Syncs source/shaders/config to the Pi and builds natively there. Full-res
# clip footage is excluded by default -- see .rsyncfilter.
#
# Usage:
#   ./scripts/deploy-to-pi.sh              # sync + build
#   ./scripts/deploy-to-pi.sh --restart    # ...then restart the systemd service
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -f "$REPO_DIR/.env" ]; then
    # shellcheck disable=SC1091
    source "$REPO_DIR/.env"
fi

: "${PI_HOST:?Set PI_HOST in .env (copy .env.example) before deploying.}"
: "${PI_USER:?Set PI_USER in .env (copy .env.example) before deploying.}"
: "${PI_APP_DIR:?Set PI_APP_DIR in .env (copy .env.example) before deploying.}"
: "${PI_OF_ROOT:?Set PI_OF_ROOT in .env (copy .env.example) before deploying.}"

echo "Checking $PI_USER@$PI_HOST is reachable..."
if ! ssh -o ConnectTimeout=5 "$PI_USER@$PI_HOST" true; then
    echo "Could not reach $PI_USER@$PI_HOST over SSH. Is the Pi powered on and on the network?" >&2
    exit 1
fi

echo "Syncing to $PI_HOST:$PI_APP_DIR..."
ssh "$PI_USER@$PI_HOST" "mkdir -p '$PI_APP_DIR'"
rsync -avz --delete --filter="merge $REPO_DIR/.rsyncfilter" "$REPO_DIR/" "$PI_USER@$PI_HOST:$PI_APP_DIR/"

echo "Building natively on the Pi..."
ssh "$PI_USER@$PI_HOST" "cd '$PI_APP_DIR' && make OF_ROOT='$PI_OF_ROOT' -j\$(nproc)"

echo "Syncing backend dependencies..."
# Idempotent and fast when nothing changed; the venv lives only on the Pi
# (excluded from rsync -- it's arch-specific).
ssh "$PI_USER@$PI_HOST" "cd '$PI_APP_DIR/backend' \
    && { [ -d .venv ] || python3 -m venv .venv; } \
    && .venv/bin/pip install -q -r requirements.txt"

if [ "${1:-}" = "--restart" ]; then
    echo "Restarting livepi-video-glitch.service..."
    ssh "$PI_USER@$PI_HOST" "sudo systemctl restart livepi-video-glitch.service"
    # The backend unit only exists once install-backend-unit.sh has run.
    ssh "$PI_USER@$PI_HOST" "systemctl list-unit-files livepi-backend.service --no-legend | grep -q . \
        && sudo systemctl restart livepi-backend.service || true"
fi

echo "Done."
