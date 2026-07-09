#!/usr/bin/env bash
# Syncs full-resolution clip footage to the Pi -- deliberately separate
# from deploy-to-pi.sh's regular rsync, which excludes bin/data/clips/*
# except samples/ (see .rsyncfilter) since real clips are large and don't
# change on every code tweak. No --delete: media is expensive to
# regenerate, so this only ever adds/updates, never removes clips already
# on the Pi.
#
# Usage:
#   ./scripts/sync-clips-to-pi.sh
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -f "$REPO_DIR/.env" ]; then
    # shellcheck disable=SC1091
    source "$REPO_DIR/.env"
fi

: "${PI_HOST:?Set PI_HOST in .env (copy .env.example) before syncing.}"
: "${PI_USER:?Set PI_USER in .env (copy .env.example) before syncing.}"
: "${PI_APP_DIR:?Set PI_APP_DIR in .env (copy .env.example) before syncing.}"

echo "Checking $PI_USER@$PI_HOST is reachable..."
if ! ssh -o ConnectTimeout=5 "$PI_USER@$PI_HOST" true; then
    echo "Could not reach $PI_USER@$PI_HOST over SSH. Is the Pi powered on and on the network?" >&2
    exit 1
fi

echo "Syncing bin/data/clips/ to $PI_HOST:$PI_APP_DIR/bin/data/clips/..."
rsync -avz --progress "$REPO_DIR/bin/data/clips/" "$PI_USER@$PI_HOST:$PI_APP_DIR/bin/data/clips/"

echo "Done."
