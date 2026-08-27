#!/usr/bin/env bash
# Draw a boot progress bar on the console while the appliance starts.
#
# Window A of the boot sequence (firmware -> kernel -> systemd -> X) is made
# BLACK by the cmdline/config.txt flags the provisioner writes -- no kernel log,
# no systemd unit chatter, no rainbow. Black is the right look for a venue, but
# with nothing at all on screen for ~40s a cold boot is indistinguishable from a
# dead box. This draws the one thing that fixes that: proof of progress.
#
# Deliberately ASCII on the text console rather than plymouth: plymouth's assets
# live on the read-only root and its early splash is baked into the initramfs,
# so touching it means update-initramfs as root -- one of the few genuine brick
# paths on a sealed box (docs/tech-debt.md). This needs neither.
#
# It writes to tty1 and simply stops mattering when X takes over: startx grabs
# vt2, so the moment the renderer's session appears the console is no longer
# what's on screen. From there the RENDERER owns the picture (Window B, the
# splash image) -- see ofApp's splash handling.
set -uo pipefail

TTY="${LIVEPI_BOOT_TTY:-/dev/tty1}"
STATUS="${LIVEPI_STATUS_PATH:-/tmp/livepi/status.json}"
[ -w "$TTY" ] || exit 0        # no console to draw on (dev box, serial-only): fine

# Rows/cols of the real console, so the bar sits on the bottom line whatever the
# panel is. `stty size` reads them from the tty itself; fall back to 80x24.
read -r ROWS COLS < <(stty size < "$TTY" 2>/dev/null || echo "24 80")
[ "${ROWS:-0}" -gt 4 ] 2>/dev/null || { ROWS=24; COLS=80; }
BAR_ROW=$(( ROWS - 1 ))
BAR_W=$(( COLS > 60 ? 46 : COLS - 12 ))
BAR_COL=$(( (COLS - BAR_W) / 2 ))

esc() { printf '%b' "$1" > "$TTY"; }
esc "\033[2J\033[?25l"          # clear, hide the cursor

cleanup() { esc "\033[2J\033[?25h"; }   # leave a clean black screen behind us
trap cleanup EXIT

draw() {  # $1 = 0..100
    local pct=$1 filled empty
    [ "$pct" -gt 100 ] && pct=100
    filled=$(( pct * BAR_W / 100 )); empty=$(( BAR_W - filled ))
    esc "\033[${BAR_ROW};${BAR_COL}H["
    esc "$(printf '%*s' "$filled" '' | tr ' ' '#')"
    esc "$(printf '%*s' "$empty" '' | tr ' ' '.')"
    esc "]"
}

# Real milestones, not a timer: each is something that has genuinely happened.
# Between them the bar creeps a little so it never looks wedged, but it can
# never run ahead of the milestone it has actually reached.
stage_pct() {
    [ -f "$STATUS" ] && { echo 100; return; }              # renderer drew a frame
    pgrep -x livepi-video-glitch >/dev/null 2>&1 && { echo 75; return; }
    pgrep -x Xorg >/dev/null 2>&1 && { echo 55; return; }
    systemctl is-active --quiet livepi-backend 2>/dev/null && { echo 35; return; }
    systemctl is-active --quiet local-fs.target 2>/dev/null && { echo 15; return; }
    echo 5
}

shown=0
for _ in $(seq 1 600); do        # ~120s ceiling; X owns the screen long before
    target=$(stage_pct)
    if [ "$target" -gt "$shown" ]; then
        shown=$(( shown + 1 ))
    elif [ "$shown" -lt $(( target + 8 )) ]; then
        shown=$(( shown + 1 ))   # gentle creep inside a stage
    fi
    draw "$shown"
    [ "$shown" -ge 100 ] && break
    sleep 0.2
done
