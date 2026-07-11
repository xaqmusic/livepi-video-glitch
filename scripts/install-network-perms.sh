#!/usr/bin/env bash
# Run this ON THE RASPBERRY PI. Installs the polkit rule that lets the backend's
# app user drive NetworkManager (WiFi provisioning) while headless -- without it
# every nmcli control call from the backend returns "not authorized". Safe to
# re-run. See backend/livepi_backend/network.py.
#
# Usage:
#   ./scripts/install-network-perms.sh          # app user = current user
#   ./scripts/install-network-perms.sh pi        # app user = pi
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_USER="${1:-$(whoami)}"

sed -e "s#__APP_USER__#$APP_USER#g" \
    "$REPO_DIR/polkit/50-livepi-network.rules.template" \
    | sudo tee /etc/polkit-1/rules.d/50-livepi-network.rules > /dev/null

# polkit auto-reloads rules.d, but a restart makes the grant take effect now.
sudo systemctl restart polkit 2>/dev/null || true

echo "Installed /etc/polkit-1/rules.d/50-livepi-network.rules (user=$APP_USER)."
echo "Verify with:  nmcli general permissions | grep network-control   # -> yes"
