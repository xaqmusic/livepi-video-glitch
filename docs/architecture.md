# Architecture

This is the living design doc for LivePi VideoGlitcher. It refines
`LivePi VideoGlitcher HLD.pdf` (the original concept) with a couple of
hardware-assumption corrections found during scaffolding, plus the concrete
software design. See also `pisound-hardware-notes.md`, `deploy.md`, and
`shader-authoring.md` for deeper dives on those specific topics.

## Stack

**openFrameworks (C++), 0.12.1.** oF's Linux Makefile build auto-detects
architecture (`x86_64` -> `linux64`, `aarch64` -> `linuxaarch64`) from the
*same unmodified app source* -- desktop and Pi each install their own
prebuilt-libs tarball as a sibling directory (see `scripts/setup-desktop.sh`
/ `scripts/setup-pi.sh`), but this repo's `Makefile` / `config.make` /
`addons.make` / `src/` never change between them. openFrameworks itself is
**never vendored into this repo**.

`ofxMidi` (wraps RtMidi/ALSA, actively maintained) is the MIDI library. It
delivers raw bytes -- `BeatClock` and the CC-value map in `ControlState` are
our own code, not something the addon provides.

We hand-roll the three glitch shaders rather than depending on an addon: the
addon people usually mean when they say "ofxGlitch" (`ofxPostGlitch`) hasn't
been touched since 2020, targets oF 0.7.3-era fixed-pipeline GLSL 120, and
was never ported to GL3+ or GLES -- patching it to work on both our desktop
and the Pi's GLES driver would be more work than writing the three effects
directly, and hand-rolled shaders stay exactly matched to the HLD's specific
descriptions.

## GL / GLES portability

Not free. The Pi 4's Mesa V3D driver is GLES-3.1-only (no desktop GL at all).
The Pi 5's newer V3D 7.1 driver reportedly adds desktop GL 3.1 core support
in addition to GLES. The desktop dev machine here has an RTX 4060 Ti at GL
4.6. Rather than write per-platform shader variants, `ShaderLoader`
(`src/util/ShaderLoader.h`) prepends the right `#version` header per platform
at load time, and every shader body in `bin/data/shaders/` is written in a
GLSL subset valid under both desktop `#version 150` and `#version 300 es`
(`in`/`out` qualifiers, `texture()` not `texture2D()`, explicit `fragColor`
output). See `shader-authoring.md` for the exact rules. This needs empirical
confirmation once we're actually running on Pi hardware (Phase 5) --
`ofGetGLRenderer()` / `glGetString(GL_VERSION)` should be logged at startup
to check what context we actually got.

GLFW (oF's Linux window backend) has no KMS/DRM path, so "boot straight to a
bare fullscreen GL surface with zero compositor" isn't achievable with vanilla
oF. The plan is a minimal X11 kiosk session instead -- see `deploy.md`.

## Input abstraction

`ControlState` (`src/control/ControlState.h`) is a plain per-frame snapshot:
MIDI clock tick count / beat-in-bar / bar / estimated BPM / `clockPresent`
(false means "free-run at a fallback BPM," not "freeze"); a CC-number-keyed
map plus `knobA`/`knobB` convenience fields resolved from **configured** CC
numbers (`knobA` remapped to -1..1 to express the HLD's bidirectional-knob
idea); a smoothed `audioLevel`; an edge-triggered `lastButtonEvent`, latched
for exactly one `update()` cycle (each backend clears it at the top of its
own `update()` before applying new events, so there's no separate
"consume" API to call).

`ControlSource` (`src/control/ControlSource.h`) is the abstract interface:
`setup(config)`, `update()`, `getState()`, `shutdown()`. Two implementations,
both driving the same shared `BeatClock` so beat math can never drift between
them:

- **`MockControlSource`** -- desktop dev. A virtual 24-PPQN timer at a
  configurable BPM; keyboard keys simulate the button and both knobs (see
  README for bindings).
- **`PisoundControlSource`** -- the Pi. `ofxMidi` against the "pisound MIDI"
  ALSA port for clock/CC, `ofSoundStream` (via `ofBaseSoundInput`) against
  Pisound's ALSA capture for `audioLevel`, and a FIFO-based bridge for button
  events (see `pisound-hardware-notes.md` for why it's a FIFO and not a
  direct read). **Deliberately does not touch Pisound's onboard knobs** --
  see below.

Which backend runs is chosen at runtime by `control_source` in
`bin/data/config/app.json` (`"mock"` or `"pisound"`), not a compile flag --
useful for testing the mock backend on the Pi itself, or vice versa.

## Corrected hardware assumption: the onboard knobs

Verified directly against Blokas' own docs: Pisound's two onboard knobs are
**fixed-function analog audio trims** (input gain, output volume) -- not
software-readable, not MIDI CC, not general-purpose. The HLD's "left knob
bidirectional / right knob intensity" idea can't be built on Pisound's own
knobs as originally framed.

Resolution: all "knob" control in this codebase is **MIDI CC** from whatever
is already patched into the rig (a synth's assignable CC knobs, or later a
cheap USB MIDI knob box for a dedicated on-device control) -- architecturally
identical either way, since it's just a MIDI CC source either path. Pisound's
own knobs stay outside `ControlSource` entirely. See
`pisound-hardware-notes.md`.

## Glitch effect chain

`ShaderChain` (`src/fx/ShaderChain.h`) runs an ordered list of `ShaderPass`
stages over ping-pong FBOs -- pass N's output becomes pass N+1's input. Each
pass receives both the live `ControlState` (beat/knobs/audio, changes every
frame) and the current `Scene` (per-scene preset intensities, changes only on
a scene switch), and combines the two however makes sense for that effect.

- **`HSyncTearPass`** -- Horizontal Sync Displacement. Shifts scanlines
  sideways by a noise field, with a hard decaying spike right after each
  beat ("the video frame snaps sideways and rips apart").
- **`ChromaticAberrationPass`** -- pulls red/blue apart, separation driven by
  a blend of live audio level and the scene's configured intensity.
- **`StutterBufferPass`** -- keeps a small ring buffer of recent frames so it
  can freeze on one or rapidly loop the last few on trigger. This one is
  architecturally different from the other two: it's mostly CPU-side frame
  selection, not a real per-pixel shader effect (`stutter_hold.frag` is a
  pure passthrough).

The default trigger for the stutter effect (every 16th note, i.e. every 6
MIDI clock ticks) is a placeholder -- exact beat-to-event mapping is scene
content design, not architecture, and will get tuned once we're iterating on
real footage (Phase 2/4 below).

## Asset strategy

No git-lfs. The HLD's actual "share a shader as a text file with a friend"
need is already served by plain-text shaders in git, and LFS would add setup
cost on every machine (including the Pi) for binary-history benefits this
project doesn't need yet. `bin/data/clips/` (full-resolution gig footage) is
gitignored; a handful of small sample clips are meant to live in
`bin/data/clips/samples/` so the repo runs from a fresh clone with zero
external fetch step (see `bin/data/clips/samples/README.md` -- these still
need to actually be added, they weren't fabricated during scaffolding).

## Phased roadmap

0. **Bootstrap** (desktop) -- scaffold this tree; install oF 0.12.1
   `linux64` as a sibling dir; confirm a stock example compiles/runs.
1. **Desktop playback + one hardcoded glitch** -- `ClipPlayer` loops a sample
   clip through `ShaderChain`; proves the FBO ping-pong + `ShaderLoader`
   shim end-to-end.
2. **Mock-driven beat counter + scenes** -- `MockControlSource`,
   `SceneManager` cycling on the fake button, debug overlay of live
   beat/bar/BPM/knob/CC values (already wired into `ofApp`, toggle with `d`).
3. **Real MIDI clock/CC** (still desktop) -- validate `ofxMidi`/`BeatClock`
   against any real/USB MIDI source; Pisound's MIDI is just an ALSA device,
   so this fully exercises the real-protocol path without needing hardware.
4. **Full glitch chain** -- tune all three effects against real footage,
   real audio level via the desktop's own `ofSoundStream` as a stand-in for
   Pisound.
5. **First real Pi deploy** -- `scripts/setup-pi.sh` / `deploy-to-pi.sh`,
   `PisoundControlSource`, empirically confirm the actual GL context and
   adjust `ShaderLoader` if reality differs from the notes above.
6. **Performance + autostart + gig polish** -- profile on-Pi frame time,
   validate the systemd kiosk unit, graceful free-run if MIDI clock drops
   mid-set, write a gig-day checklist.

## Open decisions

- Target resolution/framerate -- start profiling around 720p given the Pi's
  GLES-class GPU.
- Exact Pi model (4 vs 5) -- changes the GL-context expectation above; only
  matters starting Phase 5.
- Display method at the gig -- HDMI monitor/projector, or an actual
  composite/CRT output (Pi 4/5 dropped the analog composite jack Pi 3 had).
- Scene/preset creative content -- how many scenes, exact beat-to-event
  mapping beyond the placeholder above.
