#!/usr/bin/env bash
# Put the owner's splash image on screen as early as the kernel allows, and keep
# a "LivePi is booting" spinner under it until the renderer takes over.
#
# THE PROBLEM THIS SOLVES. Window A of the boot is deliberately black (the
# cmdline/config.txt flags the provisioner writes), and X then takes ~30-40s to
# initialise on a Pi 5 before the renderer can draw anything. Roughly a minute
# of black is indistinguishable from a dead box.
#
# HOW. The image is written straight to /dev/fb0 with ffmpeg -- already a
# dependency, so no new packages, no plymouth, and nothing in the initramfs
# (changing that on a sealed read-only-root box is a genuine brick path). X is
# then started with `-background none` by the kiosk unit, so it does NOT paint
# over the framebuffer and the image survives into X's startup.
#
# WHAT THIS DOES NOT DO: hold the console in front of X. That was tried and
# reverted -- X refuses to give a client a GL context while its VT is inactive
# ("AIGLX: Suspending AIGLX clients for VT switch"), so the renderer can never
# draw and the handover deadlocks. See docs/tech-debt.md.
set -uo pipefail

TTY="${LIVEPI_BOOT_TTY:-/dev/tty1}"
STATUS="${LIVEPI_STATUS_PATH:-/tmp/livepi/status.json}"
APP_DIR="${LIVEPI_APP_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
DATA_DIR="${LIVEPI_DATA_DIR:-$APP_DIR/bin/data}"
MESSAGE="${LIVEPI_BOOT_MESSAGE:-LivePi is booting}"
SPINNER='|/-\'
CEILING_SECS="${LIVEPI_BOOT_CEILING:-180}"
FB="${LIVEPI_FB:-/dev/fb0}"

# ---------------------------------------------------------------------------
# Resolve the splash image from the SAME config the renderer reads, so the two
# can never disagree about which image the owner chose.
# ---------------------------------------------------------------------------
splash_path() {
    python3 - "$APP_DIR" "$DATA_DIR" <<'PY' 2>/dev/null
import json, os, sys
app, data = sys.argv[1], sys.argv[2]
# Same precedence the RENDERER uses, or the framebuffer splash and the GL splash
# would end up being different pictures: the owner's gear-menu choice in
# settings.json first, then app.json's shipped default for an unconfigured box.
rel = ""
try:
    with open(os.path.join(data, "settings.json")) as f:
        rel = json.load(f).get("splashImage") or ""
except (OSError, ValueError):
    pass
if not rel:
  for name in ("app.local.json", "app.json"):          # local override wins
      try:
          with open(os.path.join(app, "bin/data/config", name)) as f:
              rel = json.load(f).get("ui", {}).get("splash_image") or rel
      except (OSError, ValueError):
          pass
      if rel:
          break
if rel:
    full = os.path.join(data, rel)
    if os.path.isfile(full):
        print(full)
PY
}

draw_image() {  # -> 0 if something landed on the framebuffer
    local img="$1" geo w h bpp pixfmt
    [ -n "$img" ] && [ -w "$FB" ] || return 1
    command -v ffmpeg >/dev/null 2>&1 || return 1
    geo=$(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null) || return 1
    w=${geo%,*}; h=${geo#*,}
    bpp=$(cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null || echo 32)
    case "$bpp" in
        16) pixfmt=rgb565le ;;
        32) pixfmt=bgra ;;
        24) pixfmt=bgr24 ;;
        *)  return 1 ;;   # unknown depth: better black than a corrupt screen
    esac
    # Contain-fit and centre on black, matching how the RENDERER draws the same
    # image -- so the handover from framebuffer to GL is not a visible jump.
    ffmpeg -loglevel error -i "$img" \
        -vf "scale=${w}:${h}:force_original_aspect_ratio=decrease,pad=${w}:${h}:(ow-iw)/2:(oh-ih)/2:black" \
        -f rawvideo -pix_fmt "$pixfmt" -frames:v 1 - > "$FB" 2>/dev/null
}

[ -w "$TTY" ] || exit 0            # no console (serial-only dev box): nothing to do

read -r ROWS COLS < <(stty size < "$TTY" 2>/dev/null || echo "24 80")
[ "${ROWS:-0}" -gt 4 ] 2>/dev/null || { ROWS=24; COLS=80; }

esc() { printf '%b' "$1" > "$TTY"; }
cleanup() { esc "\033[?25h"; }
trap cleanup EXIT

esc "\033[?25l"                     # hide the cursor

# Silence the console from here on. loglevel=3 on the kernel command line stops
# most of it, but a few messages still land on the panel and scribble across the
# splash -- kernel lines at ERR and above, and anything userspace writes to
# /dev/console. printk console level 1 mutes the first; setterm --msg off stops
# the kernel painting messages onto this VT at all. Everything still goes to the
# journal, so nothing is actually lost.
dmesg -n 1 2>/dev/null || true
setterm --msg off --blank 0 --powersave off >"$TTY" 2>/dev/null || true

IMAGE_UP=0
if draw_image "$(splash_path)"; then
    IMAGE_UP=1
    # Do NOT clear: the clear is what would wipe the image we just drew. The
    # spinner goes on the bottom row only, so it costs one line of the picture.
    MSG_ROW=$ROWS
else
    esc "\033[2J"                   # no image -- plain black with a centred message
    MSG_ROW=$(( ROWS / 2 ))
fi

draw() {  # $1 = spinner index
    local msg col
    msg="$MESSAGE  ${SPINNER:$(( $1 % 4 )):1}"
    col=$(( (COLS - ${#msg}) / 2 )); [ "$col" -lt 1 ] && col=1
    esc "\033[${MSG_ROW};1H\033[K"
    esc "\033[${MSG_ROW};${col}H${msg}"
}

spin=0
for _ in $(seq 1 $(( CEILING_SECS * 5 ))); do
    # Exit the moment the renderer has presented a frame: status.json is written
    # per frame, so its existence means the GL splash is up and this is done.
    [ -f "$STATUS" ] && break
    # Stop drawing as soon as X exists. X now takes over THIS VT (vt1) so that
    # the framebuffer image survives -- no VT switch, nothing to blank it -- and
    # two processes writing the same console would fight over it.
    pgrep -x Xorg >/dev/null 2>&1 && { sleep 0.5; continue; }
    spin=$(( spin + 1 ))
    draw "$spin"
    sleep 0.2
done
