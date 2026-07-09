#!/usr/bin/env bash
# Normalize a source video into the format this project plays best, writing
# the result into bin/data/clips/. Run on the desktop, then push with
# scripts/sync-clips-to-pi.sh.
#
# Why re-encode at all: the Pi 4's V4L2 hardware decoder + the app's
# GPU-side color conversion handle H.264 yuv420p natively end-to-end.
# Anything else costs real performance or breaks outright -- empirically:
#   - HEVC (from phone cameras) gave the GStreamer pipeline trouble on the
#     Pi; H.264 is the safe, hardware-decoded path.
#   - yuv420p decodes straight to NV12/I420, which the app now uploads
#     as-is and converts on the GPU (see ClipPlayer::load). 4:2:2/4:4:4 or
#     10-bit sources would force a software conversion back onto the CPU.
#   - 1080p+ decode wastes CPU/memory bandwidth the effects chain wants;
#     the output is capped at 720p (plenty for the 800x480 display, with
#     headroom for a bigger screen later).
#
# Usage: scripts/import-clip.sh <source-video> [output-name]
#   output-name defaults to the source basename, .mp4 extension.
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <source-video> [output-name]" >&2
    exit 1
fi

SRC="$1"
[ -f "$SRC" ] || { echo "No such file: $SRC" >&2; exit 1; }

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLIPS_DIR="$REPO_DIR/bin/data/clips"

NAME="${2:-$(basename "${SRC%.*}").mp4}"
[[ "$NAME" == *.mp4 ]] || NAME="$NAME.mp4"
OUT="$CLIPS_DIR/$NAME"

if [ -e "$OUT" ]; then
    echo "Refusing to overwrite existing $OUT -- pass a different output name." >&2
    exit 1
fi

# scale=-2:min(720,ih): cap height at 720, never upscale, keep aspect (width
# rounded to even, required by yuv420p). -g 30: a keyframe every ~second so
# the looping seek back to frame 0 restarts cleanly. -an: the app never
# plays clip audio (it *listens* on the mic input instead).
ffmpeg -hide_banner -i "$SRC" \
    -vf "scale=-2:'min(720,ih)'" \
    -c:v libx264 -profile:v high -preset medium -crf 20 \
    -pix_fmt yuv420p -g 30 \
    -an -movflags +faststart \
    "$OUT"

echo ""
echo "Imported: $OUT"
echo "Push to the Pi with: scripts/sync-clips-to-pi.sh"
