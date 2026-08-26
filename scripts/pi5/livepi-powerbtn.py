#!/usr/bin/env python3
"""LivePi power-button gesture daemon for boxes with no Pisound HAT (Pi 5).

WHY TAPS AND NOT HOLDS. The Pisound map used hold durations (3s debug, >7s
card, >=30s password reset). That is impossible here: the Pi 5's PMIC force-
powers-off a sustained press in HARDWARE, below Linux, with no way to veto it.
Measured on a Pi 5 Rev 1.1: a 4.86s hold survived, a slightly longer one killed
the board mid-press. A performer holding "a bit too long" would drop the show,
so every gesture is a TAP COUNT and holds are deliberately inert.

The gesture map itself is NOT reimplemented here -- each resolved gesture shells
out to scripts/pisound/livepi-btn.sh, the same bridge the Pisound button uses,
so there is one gesture contract with two input sources.

Runs as root: reading the button needs only the `input` group, but writing
/sys/class/leds/*/brightness needs root.
"""
import os
import select
import struct
import subprocess
import sys
import time

DEVICE = "/dev/input/by-path/platform-pwr_button-event"
BRIDGE = os.environ.get("LIVEPI_BTN_BRIDGE", "/usr/local/pisound/scripts/pisound-btn/livepi-btn.sh")

EV_KEY, KEY_POWER = 0x01, 116
EVENT_FMT = "llHHi"                    # input_event on 64-bit
EVENT_SIZE = struct.calcsize(EVENT_FMT)

# A tap is short. Measured: a deliberate quick press is ~0.11s. Anything longer
# than this is a hold, which is not a gesture -- it cancels the pending count so
# a fumbled long press can't advance a scene.
MAX_TAP_SECS = 0.6
# How long to wait after a release for another tap -- ASYMMETRIC on purpose.
#
# A single tap is the scene advance, the one gesture that happens mid-performance
# and the one place latency is felt, so it resolves after just 180ms. Once a
# SECOND tap lands we already know this isn't a scene change, nobody is waiting
# on it, and the extra 100ms buys a much more forgiving multi-tap.
#
# The trade, accepted deliberately: a sloppy double-tap with a gap over 180ms
# resolves as two separate scene advances rather than a debug toggle. Measured
# tap length is ~0.11s, so a deliberate double sits comfortably inside 180ms.
TAP_WINDOW_FIRST_SECS = 0.18
TAP_WINDOW_MORE_SECS = 0.28
# Past this, warn on the LED that a force-off is coming (~5s on a Pi 5).
HOLD_WARN_SECS = 1.5

# Tap count -> bridge action. Gaps are intentional: 4 taps does nothing, which
# makes the 5-tap recovery gesture hard to reach by accident.
GESTURES = {
    1: ["scene"],          # next scene -- the common one
    2: ["debug"],          # toggle the debug overlay
    3: ["card"],           # toggle the setup / QR card
    5: ["reset"],          # password recovery back to the factory code
}

LED_FEEDBACK = "/sys/class/leds/PWR"   # free by default (trigger [none])
LED_STATUS = "/sys/class/leds/ACT"     # second colour, if the board has one


def led(path, on):
    """Best-effort: a board without this LED must not take the daemon down."""
    try:
        with open(os.path.join(path, "brightness"), "w") as f:
            f.write("1" if on else "0")
    except OSError:
        pass


def led_trigger_off(path):
    try:
        with open(os.path.join(path, "trigger"), "w") as f:
            f.write("none")
    except OSError:
        pass


def blink(path, times, on_secs=0.06, off_secs=0.09):
    for _ in range(times):
        led(path, True)
        time.sleep(on_secs)
        led(path, False)
        time.sleep(off_secs)


def log(msg):
    print(msg, flush=True)


def dispatch(count):
    action = GESTURES.get(count)
    if not action:
        log(f"{count} taps -- no gesture bound, ignoring")
        blink(LED_FEEDBACK, 1, 0.02, 0.05)
        return
    log(f"{count} taps -> {action[0]}")
    # Confirm on the LED BEFORE shelling out, so feedback is instant even if the
    # renderer is slow to answer (or absent -- the bridge caps its FIFO wait).
    blink(LED_FEEDBACK, count if count <= 5 else 5)
    try:
        subprocess.run([BRIDGE, *action], timeout=5, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        log(f"bridge {BRIDGE} failed: {exc}")


def main():
    if not os.path.exists(DEVICE):
        log(f"no power button at {DEVICE} -- nothing to do")
        return 0
    # Take the feedback LED off whatever trigger owns it. ACT is left alone: on
    # Pi OS it carries the mmc0 (SD activity) trigger, which is a genuinely
    # useful diagnostic and not ours to steal.
    led_trigger_off(LED_FEEDBACK)
    led(LED_FEEDBACK, False)
    log(f"watching {DEVICE} (tap window {TAP_WINDOW_FIRST_SECS}s first / "
        f"{TAP_WINDOW_MORE_SECS}s after, max tap {MAX_TAP_SECS}s)")

    taps = 0
    deadline = None      # when the current tap sequence resolves
    pressed_at = None
    warned = False

    with open(DEVICE, "rb", buffering=0) as dev:
        while True:
            # Wake either on an event or when the tap window expires.
            timeout = None
            if pressed_at is not None:
                timeout = 0.05                      # ticking for the hold warning
            elif deadline is not None:
                timeout = max(0.0, deadline - time.monotonic())
            ready, _, _ = select.select([dev], [], [], timeout)

            if ready:
                data = dev.read(EVENT_SIZE)
                if data and len(data) == EVENT_SIZE:
                    _, _, etype, code, value = struct.unpack(EVENT_FMT, data)
                    if etype == EV_KEY and code == KEY_POWER:
                        if value == 1:              # press
                            pressed_at = time.monotonic()
                            warned = False
                            led(LED_FEEDBACK, True)
                        elif value == 0 and pressed_at is not None:   # release
                            held = time.monotonic() - pressed_at
                            pressed_at = None
                            led(LED_FEEDBACK, False)
                            if held > MAX_TAP_SECS:
                                # A hold is not a gesture. Drop any pending taps
                                # so a long press can't resolve as a short one.
                                log(f"hold of {held:.2f}s ignored (taps only)")
                                taps, deadline = 0, None
                            else:
                                taps += 1
                                window = (TAP_WINDOW_FIRST_SECS if taps == 1
                                          else TAP_WINDOW_MORE_SECS)
                                deadline = time.monotonic() + window

            # Warn on the LED that a force-off is approaching.
            if pressed_at is not None:
                held = time.monotonic() - pressed_at
                if held > HOLD_WARN_SECS and not warned:
                    warned = True
                    log("long press -- the PMIC will cut power at ~5s")
                if warned:
                    led(LED_FEEDBACK, int(time.monotonic() * 12) % 2 == 0)

            # Tap window expired: resolve.
            if pressed_at is None and deadline is not None and time.monotonic() >= deadline:
                count, taps, deadline = taps, 0, None
                if count:
                    dispatch(count)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        led(LED_FEEDBACK, False)
