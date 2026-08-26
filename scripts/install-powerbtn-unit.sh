#!/usr/bin/env bash
# Run this ON THE RASPBERRY PI. Installs the power-button gesture daemon for a
# box with NO Pisound HAT (Pi 5). Renders the unit template with the real repo
# path, installs the logind override that stops a tap shutting the box down, and
# puts the shared gesture bridge where the unit expects it. Safe to re-run.
#
# Usage:  ./scripts/install-powerbtn-unit.sh
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRIDGE_DIR=/usr/local/livepi
BRIDGE="$BRIDGE_DIR/livepi-btn.sh"

# The power key belongs to logind by default and powers the box off on a short
# press -- unacceptable when it's the appliance's only control surface.
sudo install -d /etc/systemd/logind.conf.d
sudo tee /etc/systemd/logind.conf.d/10-livepi-powerkey.conf > /dev/null <<'CONF'
# Managed by LivePi (scripts/install-powerbtn-unit.sh). The power button is an
# APPLIANCE control (tap = next scene), not a shutdown switch. A read-only-root
# box is safe to cut power to by design, so losing button-initiated shutdown
# costs little. HandlePowerKeyLongPress too: the PMIC's ~5s force-off is a
# HARDWARE function and cannot be vetoed here, but systemd must not pile on.
[Login]
HandlePowerKey=ignore
HandlePowerKeyLongPress=ignore
CONF

# The bridge normally lives under the Pisound install tree; on a box with no
# Pisound there is no such tree, so keep our own copy.
sudo install -D -m 755 "$REPO_DIR/scripts/pisound/livepi-btn.sh" "$BRIDGE"

sed -e "s#__APP_DIR__#$REPO_DIR#g" -e "s#__BRIDGE__#$BRIDGE#g" \
    "$REPO_DIR/systemd/livepi-powerbtn.service.template" \
    | sudo tee /etc/systemd/system/livepi-powerbtn.service > /dev/null

sudo systemctl daemon-reload
sudo systemctl restart systemd-logind

echo "Installed /etc/systemd/system/livepi-powerbtn.service (app_dir=$REPO_DIR, bridge=$BRIDGE)."
echo "Enable + start it with:"
echo "  sudo systemctl enable --now livepi-powerbtn"
