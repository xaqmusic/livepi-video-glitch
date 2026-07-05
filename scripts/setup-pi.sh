#!/usr/bin/env bash
# Run this ON THE RASPBERRY PI (Raspberry Pi OS Lite, 64-bit, Trixie --
# see docs/deploy.md for why). Installs openFrameworks (linuxaarch64),
# Pisound's driver/software support, and the ofxMidi addon. Safe to re-run.
set -euo pipefail

OF_VERSION="0.12.1"
OF_PLATFORM="linuxaarch64"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OF_ROOT="${OF_ROOT:-$REPO_DIR/../openFrameworks}"

if [ -d "$OF_ROOT" ]; then
    echo "openFrameworks already present at $OF_ROOT, skipping download."
else
    TARBALL="of_v${OF_VERSION}_${OF_PLATFORM}_gcc6_release.tar.gz"
    URL="https://openframeworks.cc/versions/v${OF_VERSION}/${TARBALL}"
    echo "Downloading openFrameworks ${OF_VERSION} for ${OF_PLATFORM}..."
    curl -fL "$URL" -o "/tmp/${TARBALL}"
    mkdir -p "$(dirname "$OF_ROOT")"
    tar -xzf "/tmp/${TARBALL}" -C "$(dirname "$OF_ROOT")"
    mv "$(dirname "$OF_ROOT")/of_v${OF_VERSION}_${OF_PLATFORM}_gcc6_release" "$OF_ROOT"
    rm "/tmp/${TARBALL}"
fi

echo "Installing Linux dependencies (requires sudo)..."
bash "$OF_ROOT/scripts/linux/debian/install_dependencies.sh" 2>/dev/null || \
    bash "$OF_ROOT/scripts/linux/ubuntu/install_dependencies.sh"

echo "Cloning ofxMidi addon..."
ADDON_DIR="$OF_ROOT/addons/ofxMidi"
if [ -d "$ADDON_DIR" ]; then
    echo "ofxMidi already present, skipping clone."
else
    git clone --depth 1 https://github.com/danomatika/ofxMidi.git "$ADDON_DIR"
fi

echo "Installing Pisound driver/software support..."
curl https://blokas.io/pisound/install.sh | sh

echo ""
echo "Next steps:"
echo "  sudo pisound-config     -- confirm MIDI/audio are recognized, wire up the button (docs/pisound-hardware-notes.md)"
echo "  make OF_ROOT=\"$OF_ROOT\"  -- build"
echo "See docs/deploy.md for the autostart/kiosk systemd setup."
