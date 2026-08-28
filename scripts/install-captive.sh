#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
# Run this ON THE RASPBERRY PI. Installs the captive-portal pieces that make a
# phone joining the AP auto-open the LivePi UI: the DNS hijack for OS probe
# domains (NetworkManager's shared-mode dnsmasq) and the :80 -> :8080 redirect
# (nftables via a systemd oneshot). The probe responders live in the backend
# (livepi_backend/captive.py). Safe to re-run. See docs/distribution.md.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Installing DNS hijack for OS captive-probe domains..."
sudo install -D -m 644 \
    "$REPO_DIR/config/networkmanager/dnsmasq-shared.d/livepi-captive.conf" \
    /etc/NetworkManager/dnsmasq-shared.d/livepi-captive.conf

echo "Installing the nftables :80 -> :8080 redirect..."
sudo install -D -m 644 "$REPO_DIR/config/livepi-captive.nft" /etc/livepi/captive.nft
sudo nft -c -f /etc/livepi/captive.nft   # syntax-check before enabling
sudo install -D -m 644 "$REPO_DIR/systemd/livepi-captive.service" \
    /etc/systemd/system/livepi-captive.service
sudo systemctl daemon-reload
sudo systemctl enable --now livepi-captive.service

echo "Done. The DNS hijack applies the next time the AP comes up"
echo "(NetworkManager reloads dnsmasq-shared.d per shared connection)."
