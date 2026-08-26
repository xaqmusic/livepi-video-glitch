#!/usr/bin/env bash
# LivePi Pisound-button bridge. The pisound-btn daemon (config: /etc/pisound.conf)
# invokes this on a button gesture; we forward the gesture to the running
# renderer over its command FIFO -- whose `click`, `debug`, and `card` verbs do
# exactly these three jobs, so wiring the button needs no renderer changes.
#
# WHICH action this run performs is taken from the script's OWN NAME, not an
# argument: pisound.conf lines are `EVENT  /path/to/script` and the daemon
# appends (click_count, hold_seconds) as $1/$2, so a config-supplied arg is
# unreliable and $1 may be a number. So the provisioner installs this once and
# symlinks three names beside it; the config points each event at a name:
#
#   CLICK_1     .../livepi-scene.sh   -- quick press: advance to the next scene
#   HOLD_3S     .../livepi-debug.sh   -- ~3s hold (3-5s): toggle the debug overlay
#   HOLD_OTHER  .../livepi-card.sh    -- long hold (>7s): toggle the setup/QR card;
#                                        a VERY long hold (>=30s) RESETS the login
#                                        password to the factory code (recovery)
#
# A Pi 5 box has no HAT and therefore no pisound-btn: scripts/pi5/livepi-powerbtn.py
# drives this same script from the PMIC power button instead, by tap count, because
# that board force-powers-off a hold at ~5s in hardware. It calls the actions by
# name (`livepi-btn.sh scene|debug|card|reset`), which is why `reset` exists as an
# explicit action rather than only as the Pisound path'''s 30s-hold side effect --
# one gesture contract, two input sources.
#
# (Run directly as `livepi-btn.sh scene|debug|card` it still works, for manual
# testing.) The pisound.button_fifo path in PisoundControlSource stays available
# but is unused here -- one channel is simpler and `click` mirrors the button.
#
# Runs as root (the daemon does); the FIFO is world-readable / owner-writable but
# root bypasses that. Best-effort and bounded: a press before the renderer is up,
# or with no reader attached, must never wedge the button daemon.
set -euo pipefail

# Action comes from the invocation name (livepi-scene.sh -> scene, etc.); fall
# back to $1 only when run as the bare livepi-btn.sh (manual testing).
name="$(basename "$0")"
case "$name" in
    *scene*) ACTION="scene" ;;
    *debug*) ACTION="debug" ;;
    *card*)  ACTION="card"  ;;
    *)       ACTION="${1:-}" ;;
esac

# Recovery: revert the web login to the factory device code by dropping the
# user-set override + the claimed marker. The AP key and SSH password already ARE
# the factory code, so this just re-unifies and re-reveals it -- no regeneration,
# no restart. Runs as root (both callers do).
do_password_reset() {
    rm -f /data/auth.json /data/.claimed 2>/dev/null || true
    logger -t livepi-btn "password reset: reverted web login to the factory code"
}

# Must match ipc.command_fifo in bin/data/config/app.json.
COMMAND_FIFO="${LIVEPI_COMMAND_FIFO:-/tmp/livepi/command.fifo}"

case "$ACTION" in
    scene) LINE="click" ;;   # -> SceneManager button Click -> next scene
    debug) LINE="debug" ;;   # -> toggle the on-screen debug overlay
    reset)
        # Explicit password recovery, for callers that can't express a 30s hold.
        # Identical effect to the `card` branch'''s >=30s path below.
        do_password_reset
        LINE="card on"
        ;;
    card)
        # pisound-btn appends (click_count, hold_seconds) to a HOLD action's
        # script. A VERY long hold (>=30s -- the Pisound blinks its MIDI LED once
        # per second so the operator can count) is the PASSWORD-RECOVERY gesture;
        # a normal long hold (7-30s) just toggles the setup/QR card.
        #
        # Defensive: check BOTH arg slots for a plausible seconds value in
        # [30,3600] (so a wrong slot/order never matters), strip any decimal, and
        # log the raw args -- a normal 7-29s hold can't land in that range, so it
        # can never accidentally reset. The journal line confirms the exact arg
        # convention after the first real hold.
        logger -t livepi-btn "card gesture args=[$*]"
        reset=0
        for a in "${1:-}" "${2:-}"; do
            a="${a%%.*}"                       # drop any fractional seconds
            case "$a" in
                ''|*[!0-9]*) : ;;              # not a plain integer -> ignore
                *) [ "$a" -ge 30 ] && [ "$a" -le 3600 ] && reset=1 ;;
            esac
        done
        if [ "$reset" = 1 ]; then
            do_password_reset
            LINE="card on"
        else
            LINE="card toggle"
        fi
        ;;
    *) echo "livepi-btn: unknown action '${ACTION}' (want: scene|debug|card|reset)" >&2; exit 2 ;;
esac

# No FIFO yet (renderer not started) -> nothing to do.
[ -p "$COMMAND_FIFO" ] || exit 0

# Opening a FIFO for write blocks until a reader exists; if the renderer isn't
# attached, cap the wait at 1s and give up rather than hang pisound-btn. When the
# renderer IS running (the normal case) the open returns immediately.
timeout 1 sh -c 'printf "%s\n" "$1" > "$2"' _ "$LINE" "$COMMAND_FIFO" 2>/dev/null || true
