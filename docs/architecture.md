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

Not free, and more varied across Pi generations than the original HLD
assumed. Raspberry Pi's GPU changed driver families between generations:
Pi 0-3 use VideoCore IV via Mesa's `vc4` driver, whose solid baseline is
GLES 2.0 (GLES 3.x support arrived later and is less complete). Pi 4 uses
VideoCore VI via Mesa's `v3d` driver, GLES-3.1-only (no desktop GL at all).
Pi 5's newer V3D 7.1 driver reportedly adds desktop GL 3.1 core support in
addition to GLES. The desktop dev machine here has an RTX 4060 Ti at GL 4.6.

Rather than target the newest GLES version each specific Pi could offer
(which would mean re-deriving/re-testing this per Pi generation, and our
three effects don't need anything from GLES 3.x anyway), the Pi side
targets **GLES 2.0 uniformly across every Pi generation** -- the one
version guaranteed to run unchanged whether it's a Pi 3, 4, or 5.

`ShaderLoader` (`src/util/ShaderLoader.h`) prepends the right `#version`
header per platform at load time (`#version 150` desktop, `#version 100` +
`precision mediump float;` on the Pi). Shader bodies in `bin/data/shaders/`
are still authored once, in the modern `in`/`out`/`texture()` dialect (the
same one that would work under desktop GLSL 150 *or* GLSL ES 300) --
`ShaderLoader` mechanically rewrites that to GLSL ES 1.00's
`attribute`/`varying`/`texture2D()`/`gl_FragColor` on the Pi at load time
(`toGles2Dialect()`), rather than maintaining a second hand-written shader
dialect. See `shader-authoring.md` for the exact rules and the naming
conventions that rewrite depends on.

`main.cpp` also had a real bug caught before ever touching Pi hardware:
it constructed `ofGLWindowSettings` unconditionally, which requests
`GLFW_OPENGL_API` (desktop GL) regardless of platform -- openFrameworks'
own `ofSetupOpenGL()` branches on `TARGET_OPENGLES` to use
`ofGLESWindowSettings` (`GLFW_OPENGL_ES_API`) instead, and ours didn't.
Since the Pi has no desktop GL profile at all (Mesa `vc4`/`v3d` are
GLES-only), this would have failed to create a context at all, on any Pi
generation. Fixed to branch the same way oF's own default entry point does.

**Status on the Raspberry Pi 3 Model B Plus** (early MVP target before the
planned Pi 4/5 migration, Phase 5): the app itself builds cleanly
(aarch64, GLES 2.0 targeting, `-ljack` fix -- see git history), but hasn't
run yet -- **X.Org itself crashes** before our binary ever gets a GL
context. Reproduced identically both over SSH and from the physical
console (ruling out a session/SSH-permission cause): `Xorg` segfaults
immediately after loading `glamoregl` (glamor's EGL/GBM-based
acceleration module) while probing the `modesetting` KMS driver against
`/dev/dri/card0`. This looks like a Mesa `vc4` + glamor incompatibility on
this specific Pi 3 / Trixie / Mesa 25.0.7 combination, not anything in our
own code -- `ofGetGLRenderer()`/`glGetString(GL_VERSION)` (already logged
at startup in `ofApp::setup()`) has never actually been reached on this
hardware yet.

Two side-fixes made getting this far possible and are worth keeping
regardless of the crash: `/etc/X11/Xwrapper.config` needed
`allowed_users=anybody` (default `console` refuses to start X outside an
active console/seat login -- needed anyway for the eventual kiosk systemd
service, which won't have an interactive login present either), and
`xinit`/`xserver-xorg`/`xserver-xorg-legacy`/`x11-xserver-utils` aren't
installed by default on Raspberry Pi OS **Lite** and need adding.

Untried next step if picked back up: force X to skip glamor entirely (an
`xorg.conf` `Option "AccelMethod" "none"`, or the `fbdev` driver directly)
-- a known workaround for VC4-driver/glamor issues elsewhere. Shouldn't
affect our own app's GL context, since that's negotiated directly by
GLFW/EGL, not through X's own accelerated-2D path.

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
`setup(config)`, `update()`, `getState()`, `shutdown()`. Three
implementations, all driving the same shared `BeatClock` so beat math can
never drift between them:

- **`MockControlSource`** -- desktop dev. A virtual 24-PPQN timer at a
  configurable BPM; keyboard keys simulate the button and both knobs (see
  README for bindings).
- **`MidiControlSource`** -- desktop testing against any real MIDI device
  (USB keyboard/controller), no Pisound hardware needed. On `setup()` it
  logs every available MIDI port (`ofLogNotice`) and every incoming CC
  number/value, which doubles as a device-detection + CC-learn workflow: two
  ways to map a knob -- watch the console, wiggle the physical control, read
  off its CC number, set `midi.knobA_cc`/`midi.knobB_cc` in `app.local.json`
  (persists across restarts); or press `a`/`b` then move the knob, which
  learns and immediately applies the next CC message for the rest of that
  run (verified against real injected MIDI CC messages, not just read).
  Uses the keyboard's spacebar/`h` as a stand-in scene button Click/Hold (no
  FIFO/hardware button on a generic MIDI device). **Caveat found in
  practice:** some controllers send 14-bit high-resolution CC as an
  MSB/LSB pair (CC *n* + CC *n*+32) rather than a single 7-bit CC --
  `knobA_cc`/`knobB_cc` only read a single CC number, so map to the MSB
  (the lower-numbered one) for a working, if coarser (128-step), control.
- **`PisoundControlSource`** -- the Pi. `ofxMidi` against the "pisound MIDI"
  ALSA port for clock/CC, `ofSoundStream` (via `ofBaseSoundInput`) against
  Pisound's ALSA capture for `audioLevel`, and a FIFO-based bridge for button
  events (see `pisound-hardware-notes.md` for why it's a FIFO and not a
  direct read). **Deliberately does not touch Pisound's onboard knobs** --
  see below.

Which backend runs is chosen at runtime by `control_source` in
`bin/data/config/app.json` (`"mock"`, `"midi"`, or `"pisound"`), not a
compile flag -- useful for testing the mock backend on the Pi itself, or
vice versa.

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

### Two oF renderer bugs that silently ate every pass's shader

Getting the three passes to actually render anything (rather than a silent
passthrough or a solid color) took two separate fixes, both worth recording
since they'll bite again the moment a fourth pass is added:

1. **`modelViewProjectionMatrix` never reaches a freshly-bound custom
   shader.** oF's own per-`shader.begin()` matrix upload
   (`ofGLProgrammableRenderer::bind()` -> `uploadMatrices()`) is supposed to
   set this uniform automatically on every bind. Confirmed empirically it
   doesn't: `glGetActiveUniform` finds the uniform at a valid location, but
   reading it back with `glGetUniformfv` immediately after `shader.begin()`
   shows an all-zero matrix, which collapses every vertex to `gl_Position =
   (0,0,0,0)` and rasterizes nothing. Fix: `ShaderLoader::bindMvp(shader)`
   sets it explicitly from `ofGetCurrentMatrix(OF_MATRIX_PROJECTION) *
   ofGetCurrentMatrix(OF_MATRIX_MODELVIEW)` right after every
   `shader.begin()`.
2. **The `texcoord` vertex attribute goes disabled mid-draw.** Even with (1)
   fixed, drawing through oF's normal `ofFbo::draw()` / `ofTexture::draw()`
   (which route through the renderer's single shared internal VBO,
   `ofGLProgrammableRenderer::meshVbo`, also used by `ofMesh::draw()`) left
   every fragment reading the same `texCoordVarying` instead of an
   interpolated 0..1 gradient -- confirmed via `glGetVertexAttribiv` showing
   the array disabled and GL falling back to the generic constant
   `(1,1,1,1)`. Root cause not fully pinned down (this process also drives
   GStreamer/NVDEC GL interop for hardware video decode on the same
   context, a plausible source of shared-state interference), but the fix
   sidesteps it regardless: `ShaderLoader::drawFullscreenQuad(w, h)` draws
   through its own dedicated VAO/VBO with explicit
   `glVertexAttribPointer`/`glEnableVertexAttribArray` calls, never
   touching oF's shared mesh machinery.

Every `ShaderPass::apply()` must call `bindMvp()` right after
`shader.begin()` and use `drawFullscreenQuad()` instead of
`someFbo.draw(0, 0)` for the shader-bound draw. The plain (non-custom-shader)
copy in `StutterBufferPass` (into its ring buffer) is unaffected and still
uses the normal `ofFbo::draw()`.

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
   `linux64` as a sibling dir; confirm a stock example compiles/runs. **Done**
   -- went straight to a first build of the real app instead of a stock
   example; caught two missing-include bugs (`ofSoundBuffer.h`,
   `ofAppRunner.h`) and a dead openFrameworks download URL (moved to GitHub
   Releases) along the way, see git history.
1. **Desktop playback + one hardcoded glitch** -- `ClipPlayer` loops a sample
   clip through `ShaderChain`; proves the FBO ping-pong + `ShaderLoader`
   shim end-to-end. **Done** -- verified with a synthetic placeholder clip
   (`bin/data/clips/samples/sample_crt_loop_01.mp4`, not real footage yet).
2. **Mock-driven beat counter + scenes** -- `MockControlSource`,
   `SceneManager` cycling on the fake button, debug overlay of live
   beat/bar/BPM/knob/CC values (already wired into `ofApp`, toggle with `d`).
   **Done** -- confirmed live in the same smoke test as Phase 1, since the
   mock-driven scaffold was built as one whole rather than incrementally.
3. **Real MIDI clock/CC** (still desktop) -- validate `ofxMidi`/`BeatClock`
   against any real/USB MIDI source; Pisound's MIDI is just an ALSA device,
   so this fully exercises the real-protocol path without needing hardware.
   **Done** -- `MidiControlSource` added for this; verified against a real
   USB MIDI keyboard's filter-cutoff knob (see "Input abstraction" above for
   the 14-bit CC finding). No MIDI clock from that particular keyboard, so
   the free-run fallback path got exercised too, not just the happy path.
4. **Full glitch chain** -- tune all three effects against real footage,
   real audio level via the desktop's own `ofSoundStream` as a stand-in for
   Pisound. **In progress** -- `MidiControlSource` now also captures real
   audio level (same class, since the desktop MIDI keyboard's audio
   interface -- a UMC404HD -- already has real audio I/O; verified
   `audioLevel` responds live to real sound). Also fixed two real bugs
   found along the way: `StutterBufferPass`'s trigger wasn't gated on
   `clockPresent`, so with no MIDI clock present `midiClockTicks` stays at
   0 forever and `0 % 6 < 2` was permanently true -- the stutter effect was
   *always* engaged instead of gracefully idle; and all three passes'
   shaders were never actually loaded (`ofApp` called `shaderChain.setup()`
   before `addPass()`, so the `for (auto& pass : passes) pass->setup()`
   loop ran over an empty vector) -- see "Two oF renderer bugs" above for
   what surfaced once that was fixed and shaders actually started
   rendering. All three effects now visibly confirmed working together
   against the colorbars test scene, and against a real 720p clip
   (`silhouette` scene) -- chromatic aberration and h-sync both read clearly
   against its high-contrast edges. knobA is now wired in too, as a master
   glitch-intensity knob shared across all three passes (remapped from its
   native -1..1 bidirectional range to 0..1: fully counterclockwise kills
   every effect, fully clockwise is each scene's configured intensity) --
   confirmed via keyboard hotkeys (`[`/`]` for knobA, `,`/`.` for knobB, same
   bindings as `MockControlSource`, added to `MidiControlSource` since both
   knobs aren't CC-learned from the real MIDI keyboard yet). Also confirmed
   `StutterBufferPass`'s trigger actually activates on schedule (not just
   correctly idles without one) using `MockControlSource`'s virtual 24-PPQN
   clock. Still open: tuning intensities further against real footage.
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
