#!/usr/bin/env bash
# Run this ON THE RASPBERRY PI. Renders
# systemd/livepi-video-glitch.service.template with the account and repo
# path actually in use and installs it -- no manual editing of the unit
# file needed. Safe to re-run (e.g. after moving the repo, or to switch
# which account runs the kiosk).
#
# Usage:
#   ./scripts/install-systemd-unit.sh              # runs as the current user
#   ./scripts/install-systemd-unit.sh someoneelse   # runs as a different user
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_AS_USER="${1:-$(whoami)}"

sed -e "s#__APP_DIR__#$REPO_DIR#g" -e "s#__PI_USER__#$RUN_AS_USER#g" \
    "$REPO_DIR/systemd/livepi-video-glitch.service.template" \
    | sudo tee /etc/systemd/system/livepi-video-glitch.service > /dev/null

sudo systemctl daemon-reload

echo "Installed /etc/systemd/system/livepi-video-glitch.service (user=$RUN_AS_USER, app_dir=$REPO_DIR)."
echo "Enable + start it with:"
echo "  sudo systemctl enable --now livepi-video-glitch"
