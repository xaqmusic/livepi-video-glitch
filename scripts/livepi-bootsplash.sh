#!/usr/bin/env bash
# Show "LivePi is booting" with a spinner on the console while the appliance
# starts, and keep it on the panel until the renderer has drawn its first frame.
#
# Window A of the boot (firmware -> kernel -> systemd -> X) is made BLACK by the
# cmdline/config.txt flags the provisioner writes. Black is right for a venue,
# but ~60s of nothing is indistinguishable from a dead box, so this provides the
# one thing that fixes that: proof of life.
#
# A SPINNER, NOT A PROGRESS BAR. A bar implies a known total, and this boot has
# no honest one -- X's glamor init alone varies by tens of seconds. Worse, an
# earlier version advanced the bar 1% per tick and refused to exit until it read
# 100%, which held the console in front for ~24s AFTER the renderer was ready
# and hid the splash image completely. A spinner promises only "alive", which is
# all we can truthfully promise, and lets this exit the instant it should.
#
# THE MESSAGE ENDS WHEN X STARTS, and that is a hard limit, not an oversight.
# X grabs its own VT, switching the panel away from this console, so the screen
# is black for X's startup (~30s on a Pi 5, dominated by glamor init on V3D)
# until the renderer draws its splash.
#
# Holding the console in front of X was tried and REVERTED. It deadlocks: X
# logs "AIGLX: Suspending AIGLX clients for VT switch" and refuses to give a
# client a GL context while its VT is inactive, so the renderer never draws,
# never writes status.json, and this script never hands the panel back. An
# earlier test appeared to prove it safe only because it switched away AFTER the
# renderer already held a context -- a different situation entirely.
#
# Covering that window needs something that draws to the FRAMEBUFFER before X
# starts (fbi or similar), not a VT trick.
#
# Deliberately not plymouth: its assets live on the read-only root and its early
# splash is baked into the initramfs, so changing it means update-initramfs as
# root -- one of the few genuine brick paths on a sealed box.
set -uo pipefail

TTY="${LIVEPI_BOOT_TTY:-/dev/tty1}"
STATUS="${LIVEPI_STATUS_PATH:-/tmp/livepi/status.json}"
MESSAGE="${LIVEPI_BOOT_MESSAGE:-LivePi is booting}"
SPINNER='|/-\'
CEILING_SECS="${LIVEPI_BOOT_CEILING:-180}"

[ -w "$TTY" ] || exit 0           # no console to draw on (serial-only dev box)

read -r ROWS COLS < <(stty size < "$TTY" 2>/dev/null || echo "24 80")
[ "${ROWS:-0}" -gt 4 ] 2>/dev/null || { ROWS=24; COLS=80; }
MSG_ROW=$(( ROWS / 2 ))           # centred vertically now there is no bar

esc() { printf '%b' "$1" > "$TTY"; }

# Whatever happens below -- normal exit, ceiling, kill -- hand the panel to X.
# A stranded console is a worse failure than a missing message.
cleanup() { esc "\033[r\033[2J\033[?25h"; }   # leave a clean black console behind
trap cleanup EXIT

esc "\033[2J\033[?25l"                       # clear, hide the cursor
esc "\033[1;$(( MSG_ROW - 1 ))r"             # keep stray output above the message

draw() {  # $1 = spinner index
    local msg col
    msg="$MESSAGE  ${SPINNER:$(( $1 % 4 )):1}"
    col=$(( (COLS - ${#msg}) / 2 )); [ "$col" -lt 1 ] && col=1
    esc "\033[${MSG_ROW};1H\033[K"
    esc "\033[${MSG_ROW};${col}H${msg}"
}

spin=0
ticks=$(( CEILING_SECS * 5 ))                # 200ms per tick
for _ in $(seq 1 "$ticks"); do
    # Exit the moment the renderer has presented a frame: status.json is written
    # per frame, so its existence means the splash is on screen and the panel
    # should be handed over NOW rather than after some cosmetic countdown.
    [ -f "$STATUS" ] && break
    spin=$(( spin + 1 ))
    draw "$spin"
    # Periodically wipe anything that landed above us.
    [ $(( spin % 25 )) -eq 0 ] && esc "\033[1;1H\033[J"
    sleep 0.2
done
