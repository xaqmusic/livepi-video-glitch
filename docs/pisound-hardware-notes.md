# Pisound hardware notes

Findings from Blokas' own docs (https://blokas.io/pisound/docs/), checked
directly before building `PisoundControlSource` around them.

## MIDI in/out, audio in/out -- plain ALSA, no surprises

Pisound's MIDI in/out shows up as a normal ALSA MIDI sequencer/rawmidi device
("pisound MIDI"). Its audio in/out is a normal ALSA sound card. No custom
driver work needed -- `ofxMidi` and `ofSoundStream` see it like any other
interface. This part of the HLD was accurate.

## The onboard knobs -- fixed-function, not general-purpose

The two onboard potentiometers are **input GAIN (0-40dB trim)** and **output
VOLUME**. They sit directly inline with the analog audio path. There is no
ALSA mixer control, sysfs file, or MIDI CC exposing their position to
software -- they cannot be read by an application at all.

This contradicts the HLD's framing of "left knob (bidirectional/center
detent) / right knob (intensity)" as general performance controls sourced
from the Pisound board itself. `PisoundControlSource` does not attempt to
read them. Instead, `knobA`/`knobB` in `ControlState` come from **configured
MIDI CC numbers** -- either an external synth's assignable CC knobs (already
part of the HLD's own MIDI CC modulation idea) or, later, a small USB MIDI
knob box plugged into the Pi if dedicated on-device knobs (independent of
whatever synth happens to be patched in) turn out to matter. Both are the
same code path -- just a MIDI CC source -- so this is a non-blocking, purely
cosmetic hardware decision.

## The button -- real, but reached indirectly

There's no "read button state from my app" API. Physically, a kernel
GPIO-interrupt-driven daemon (`pisound-btn`) dispatches to user-installed
shell scripts based on click pattern (single/double/triple/4-8x click, and
several hold-duration buckets), configured via `sudo pisound-config`.

Integration shape used here: `scripts/pisound/livepi-btn.sh` is installed once
under `/usr/local/pisound/scripts/pisound-btn/` and symlinked as
`livepi-{scene,debug,card}.sh`; each name is wired to an event in
`/etc/pisound.conf` -- single click -> next scene, ~3s hold -> toggle the debug
overlay, >7s hold -> toggle the setup/QR card. The script forwards the gesture
to the renderer's command FIFO (`ipc.command_fifo`), whose `click`/`debug`/
`card` verbs already do those jobs, so the button needs no renderer changes. It
selects its action from its own basename because pisound-btn passes no argument
(confirmed on hardware: the daemon logs `args = ''`, and a >7s hold fires only
the long bucket -- it does not trip the 3s action on the way past). The image
build wires all of this (`scripts/provision-appliance.sh`), explicitly pointing
the buckets it doesn't use -- **especially `HOLD_5S`, whose stock action is
`shutdown.sh`** -- at `do_nothing.sh`. See `deploy.md` for the manual steps.

An earlier, simpler bridge -- `scripts/pisound/advance-scene-btn.sh` -- writes a
one-byte marker (`c`/`h`) into `pisound.button_fifo` (`/tmp/livepi-button.fifo`,
configurable in `bin/data/config/app.json`) that
`PisoundControlSource::pollButtonFifo()` reads once per frame. That path handles
click/hold only and is superseded by `livepi-btn.sh`.

## Pi 5 compatibility

Pisound **v1.0** has known Pi 4 issues (workaroundable via `pisound-config`);
**v1.1** is fully Pi 4 compatible; **v1.2** specifically improves Pi 5
compatibility (physical clearance for the Pi 5 Active Cooler, and addresses
early Pi 5 I2S-master/EEPROM-overlay issues some users reported on earlier
revisions). Worth confirming which revision is actually in hand before
Phase 5 -- see the open decisions in `architecture.md`.
