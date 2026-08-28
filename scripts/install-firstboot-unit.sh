#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
# Run this ON THE RASPBERRY PI. Renders
# systemd/livepi-firstboot.service.template and installs it. Safe to re-run.
#
# This is for the golden IMAGE (or a spare-card test of it), not the rsync
# dev flow -- it enables per-device provisioning on first boot. See
# docs/distribution.md and scripts/firstboot.sh.
#
# Usage:
#   ./scripts/install-firstboot-unit.sh                       # user=$(whoami), data=/data
#   ./scripts/install-firstboot-unit.sh pi                    # app user = pi
#   ./scripts/install-firstboot-unit.sh pi /data              # + explicit data dir
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_USER="${1:-$(whoami)}"
DATA_DIR="${2:-/data}"

sed -e "s#__APP_DIR__#$REPO_DIR#g" \
    -e "s#__APP_USER__#$APP_USER#g" \
    -e "s#__DATA_DIR__#$DATA_DIR#g" \
    "$REPO_DIR/systemd/livepi-firstboot.service.template" \
    | sudo tee /etc/systemd/system/livepi-firstboot.service > /dev/null

sudo systemctl daemon-reload

echo "Installed /etc/systemd/system/livepi-firstboot.service (app_user=$APP_USER, data_dir=$DATA_DIR, app_dir=$REPO_DIR)."
echo "Enable it so first boot personalizes the box:"
echo "  sudo systemctl enable livepi-firstboot"
echo "Run it now (or just reboot):"
echo "  sudo systemctl start livepi-firstboot && systemctl status livepi-firstboot"
