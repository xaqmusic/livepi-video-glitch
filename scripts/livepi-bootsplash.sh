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
# Which VT this console is, and which one X takes (pinned to vt2 by the kiosk
# unit's `startx ... -- vt2`, so this is deterministic rather than a guess).
CONSOLE_VT="${LIVEPI_CONSOLE_VT:-1}"
X_VT="${LIVEPI_X_VT:-2}"
# Never leave the panel on the console: whatever happens below, hand the screen
# to X on the way out. A stranded console is a worse failure than a missing bar.
FINAL_VT="$X_VT"
[ -w "$TTY" ] || exit 0        # no console to draw on (dev box, serial-only): fine

# Rows/cols of the real console, so the bar sits on the bottom line whatever the
# panel is. `stty size` reads them from the tty itself; fall back to 80x24.
read -r ROWS COLS < <(stty size < "$TTY" 2>/dev/null || echo "24 80")
[ "${ROWS:-0}" -gt 4 ] 2>/dev/null || { ROWS=24; COLS=80; }
BAR_ROW=$(( ROWS - 1 ))
MSG_ROW=$(( BAR_ROW - 2 ))
SCROLL_BOTTOM=$(( MSG_ROW - 1 ))
MESSAGE="LivePi is booting"
SPINNER='|/-\'
BAR_W=$(( COLS > 60 ? 46 : COLS - 12 ))
BAR_COL=$(( (COLS - BAR_W) / 2 ))

esc() { printf '%b' "$1" > "$TTY"; }
# Scrolling region stops one row short of the bar, so anything that still writes
# to this console (a late kernel line, a network daemon) scrolls ABOVE and can
# never push the bar around or leave a second copy of it on screen. Belt and
# braces with the provisioner moving getty off tty1, which is the usual culprit.
esc "\033[2J\033[?25l"          # clear, hide the cursor

esc "\033[1;${SCROLL_BOTTOM}r"    # confine scrolling above the bar
# Reset the region on the way out, or a later console user inherits it.
cleanup() {
    esc "\033[r\033[2J\033[?25h"          # leave a clean black screen behind us
    chvt "$FINAL_VT" 2>/dev/null || true
}
trap cleanup EXIT

draw() {  # $1 = 0..100, $2 = spinner index
    local pct=$1 spin=$2 filled empty msg col
    [ "$pct" -gt 100 ] && pct=100
    filled=$(( pct * BAR_W / 100 )); empty=$(( BAR_W - filled ))

    # The spinner matters more than the bar. A bar can sit still at a milestone
    # for many seconds and read as a hung box; a character that keeps turning
    # says "alive" at a glance, which is the whole job during a long black boot.
    msg="$MESSAGE  ${SPINNER:$(( spin % 4 )):1}"
    col=$(( (COLS - ${#msg}) / 2 )); [ "$col" -lt 1 ] && col=1
    esc "\033[${MSG_ROW};1H\033[K"
    esc "\033[${MSG_ROW};${col}H${msg}"

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

# X grabs its own VT the moment it starts, which switches the PANEL away from
# this console -- so the bar vanished for the whole of X's startup (~40s on a
# cold boot, dominated by glamor init on V3D) and the operator saw black with no
# sign of life. Switch the display back here and hold it until the renderer has
# actually drawn something.
#
# Verified safe: X completes its initialisation perfectly well while it is not
# the active VT, and the renderer runs at full rate and writes telemetry from
# behind it -- so nothing is being starved while the console is up front.
switched_back=0
grab_console() {
    [ "$switched_back" = "0" ] || return 0
    pgrep -x Xorg >/dev/null 2>&1 || return 0
    chvt "$CONSOLE_VT" 2>/dev/null || true
    switched_back=1
}

shown=0
for _ in $(seq 1 600); do        # ~120s ceiling, then the trap hands over to X
    grab_console
    target=$(stage_pct)
    if [ "$target" -gt "$shown" ]; then
        shown=$(( shown + 1 ))
    elif [ "$shown" -lt $(( target + 8 )) ]; then
        shown=$(( shown + 1 ))   # gentle creep inside a stage
    fi
    spin=$(( ${spin:-0} + 1 ))
    draw "$shown" "$spin"
    # Every ~2s, clear the scroll region above the bar. Cheap, and it means a
    # stray line that slips through never lingers for the whole boot.
    tick=$(( ${tick:-0} + 1 ))
    if [ $(( tick % 10 )) -eq 0 ]; then
        esc "\033[1;1H\033[J"
    fi
    [ "$shown" -ge 100 ] && break
    sleep 0.2
done
