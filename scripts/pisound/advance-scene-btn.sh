#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
# Install this under /usr/local/pisound/scripts/pisound-btn/ and wire it to a
# click pattern via `sudo pisound-config` (see docs/pisound-hardware-notes.md
# and docs/deploy.md). pisound-btn invokes scripts like this one directly on
# button events; we just forward a one-byte marker into the FIFO that
# PisoundControlSource polls each frame.
set -euo pipefail

FIFO_PATH="${LIVEPI_BUTTON_FIFO:-/tmp/livepi-button.fifo}"
EVENT="${1:-c}"  # 'c' = click, 'h' = hold -- see PisoundControlSource::pollButtonFifo

if [ -p "$FIFO_PATH" ]; then
    echo -n "$EVENT" > "$FIFO_PATH"
fi
