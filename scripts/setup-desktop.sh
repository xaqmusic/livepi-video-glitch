#!/usr/bin/env bash
# Installs openFrameworks 0.12.1 (linux64) as a sibling directory to this
# repo, its Linux dependency packages, and the ofxMidi addon. Safe to re-run.
set -euo pipefail

OF_VERSION="0.12.1"
OF_PLATFORM="linux64"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OF_ROOT="${OF_ROOT:-$REPO_DIR/../openFrameworks}"

if [ -d "$OF_ROOT" ]; then
    echo "openFrameworks already present at $OF_ROOT, skipping download."
else
    TARBALL="of_v${OF_VERSION}_${OF_PLATFORM}_gcc6_release.tar.gz"
    URL="https://github.com/openframeworks/openFrameworks/releases/download/${OF_VERSION}/${TARBALL}"
    echo "Downloading openFrameworks ${OF_VERSION} for ${OF_PLATFORM}..."
    curl -fL "$URL" -o "/tmp/${TARBALL}"
    mkdir -p "$(dirname "$OF_ROOT")"
    tar -xzf "/tmp/${TARBALL}" -C "$(dirname "$OF_ROOT")"
    mv "$(dirname "$OF_ROOT")/of_v${OF_VERSION}_${OF_PLATFORM}_gcc6_release" "$OF_ROOT"
    rm "/tmp/${TARBALL}"
fi

echo "Installing Linux dependencies (requires sudo)..."
sudo bash "$OF_ROOT/scripts/linux/ubuntu/install_dependencies.sh"

echo "Cloning ofxMidi addon..."
ADDON_DIR="$OF_ROOT/addons/ofxMidi"
if [ -d "$ADDON_DIR" ]; then
    echo "ofxMidi already present, skipping clone."
else
    git clone --depth 1 https://github.com/danomatika/ofxMidi.git "$ADDON_DIR"
fi

echo ""
echo "Done. Add this to your shell profile so 'make' picks up OF_ROOT by default:"
echo "  export OF_ROOT=\"$OF_ROOT\""
echo ""
echo "Then build with:      make OF_ROOT=\"$OF_ROOT\""
echo "Run with:             ./run.sh"
