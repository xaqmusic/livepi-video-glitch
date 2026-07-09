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

### Status on the Raspberry Pi 4 (confirmed working)

Different physical Pi from the 3 above, fresh Raspberry Pi OS Lite (64-bit,
Trixie) flash. **The Pi 3's blocker is gone here** -- X starts cleanly with
no glamor crash, GLFW gets a real GL(ES) context, and the app renders
h-sync tear visibly on the attached screen. Getting there took five more
bugs, all now automated in `scripts/setup-pi.sh` (patched into the
downloaded oF tree, idempotent, safe to re-run) rather than needing to be
rediscovered on the next fresh Pi:

1. **`minimal X11 packages missing`** -- Raspberry Pi OS Lite ships none of
   `xinit`/`xserver-xorg`/`xserver-xorg-legacy`/`x11-xserver-utils` (same
   finding as the Pi 3 section above).
2. **oF's `linuxaarch64` release never defines `TARGET_OPENGLES`.**
   `ofConstants.h`'s own platform detection checks `__ARM__` (uppercase) to
   decide GLES vs desktop GL; no real compiler defines that on 64-bit ARM
   (GCC defines `__aarch64__`). Since "linuxaarch64" is oF's generic
   64-bit-ARM target (not Pi-specific the way the old 32-bit
   "linuxarmv6l"/"linuxarmv7l" releases were), it silently falls through to
   assuming desktop GL is available. This Pi 4's Mesa `v3d` driver (much
   newer than when the "Pi 4 is GLES-only, no desktop GL at all" note above
   was researched) turns out to *also* expose a partial desktop-GL-3.1
   compatibility profile -- enough to link and create a context, but only
   up to GLSL 1.40, not our shaders' `#version 150` (GLSL 1.50 needs GL
   3.2+). Fixed by forcing `PLATFORM_DEFINES += TARGET_OPENGLES` for this
   platform.
3. **Forcing `TARGET_OPENGLES` on then exposed a second, previously-latent
   bug: `ofConstants.h`'s non-ARM Linux branch unconditionally
   `#include`s `<GL/glew.h>`** (a desktop GL function-pointer loader) even
   when `TARGET_OPENGLES` is set, because its GLES/desktop choice is gated
   on `TARGET_LINUX_ARM`, not `TARGET_OPENGLES` -- and our platform has the
   latter without the former (flipping `TARGET_LINUX_ARM` on instead would
   fix this but silently drop the `TARGET_GLFW_WINDOW`/audio defines this
   project needs, which only the non-ARM branch sets). With glew.h
   included, oF's own `glewInit()` call is correctly skipped for GLES
   builds (already properly guarded) -- but that leaves every GL function
   GLEW touches as a null, never-populated function pointer, so the first
   real GL call **segfaults at address `0x0`** (confirmed via `gdb -batch
   -ex run -ex bt`; crashed inside `ofShader::checkAndCreateProgram()`,
   i.e. the very first shader oF compiles for its own internal default
   shader collection, before `ofApp::setup()` even runs). Fixed by routing
   to the real `<GLES2/gl2.h>`/`<GLES2/gl2ext.h>` headers instead when
   `TARGET_OPENGLES` is set, matching what the `TARGET_LINUX_ARM` branch
   right above it already does.
4. **Three headers declare `EGLDisplay`/`EGLContext`/`EGLSurface`-returning
   functions under `#if defined(TARGET_LINUX) && defined(TARGET_OPENGLES)`
   without ever `#include`-ing `<EGL/egl.h>`** on Linux
   (`ofAppRunner.h`, `ofAppBaseWindow.h`, `ofAppGLFWWindow.h`) -- unlike
   `ofAppEGLWindow.h`, oF's *other* GLES window backend (used historically
   on 32-bit Pi builds), which does. This specific combination -- GLFW
   windowing plus GLES, rather than oF's dedicated EGL window class --
   appears to have never been exercised upstream. `ofAppRunner.h`'s
   declarations are free functions, so the include could go directly above
   them; `ofAppBaseWindow.h`/`ofAppGLFWWindow.h`'s are class member
   declarations, and `extern "C" {` (EGL's own header's first line) can't
   open mid-class -- those includes had to go at file scope near the top
   instead, and specifically *after* `ofConstants.h` is included, since
   that's the file that actually defines `TARGET_LINUX`/`TARGET_OPENGLES`
   in the first place.
5. **`ofFbo.cpp`'s `checkStatus()` logs `GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS`**,
   a desktop-GL-only enum core GLES 2 doesn't define, in an otherwise
   properly-guarded `switch` (every other legacy-only case right next to it
   already has a `#ifndef TARGET_OPENGLES`) -- just this one case was
   missed upstream.
6. **Our own `ShaderLoader::drawFullscreenQuad()`** (added to fix the
   desktop shader-rendering bugs, see "Two oF renderer bugs" under
   "Glitch effect chain" below) used `glGenVertexArrays`/`glBindVertexArray`
   unconditionally -- VAOs don't exist in GLES 2 core at all (GLES 3+ or an
   optional, not-guaranteed-present OES extension on GLES 2). Desktop core
   profile *requires* a bound VAO before any draw call, so one is kept
   there; on GLES, attribute pointers are just re-set every call instead of
   cached in a VAO (a few extra GL calls per pass per frame, negligible,
   correct without depending on an extension that may not be on every Pi
   generation's driver).

`docs/deploy.md`'s kiosk `startx` invocation needs a valid VT to grab --
abruptly killing a previous X server (`kill -9`, as opposed to a clean
`Ctrl+Alt+Backspace`-style shutdown) can leave the console in a state where
`xinit`'s own auto-detection of which VT to use fails with "Couldn't get a
file descriptor referring to the console." Not a real blocker (a clean
reboot or an explicit `vtN` argument to `startx` sidesteps it), but worth
knowing before assuming a fresh crash on the next test.

### Video playback: native YUV + GPU color conversion (confirmed working)

Real clips initially played at ~40% of real-time speed on the Pi 4 while
the app itself rendered a smooth 60fps. The V4L2 hardware decoder was never
the problem (a raw `gst-launch playbin` with a sync'd `fakesink` played a
10s clip in 10.001s) -- the bottleneck was that oF's video player defaults
to demanding **RGB** at its appsink, which makes playbin auto-plug a
*software* `videoconvert` doing YUV->RGB on the CPU for every frame. That
one element pegged an entire core (a `vqueue:src` GStreamer thread at
99.9%) and the picture backlogged behind the pipeline clock.

The fix keeps the pipeline in the decoder's own format end to end:

- `ClipPlayer` requests `OF_PIXELS_NATIVE`, so the appsink negotiates
  whatever the decoder produces (I420 from `v4l2h264dec` on the Pi, NV12
  from NVDEC on the desktop) and `videoconvert` becomes a passthrough.
- `ShaderChain` seeds its first FBO by **drawing the player** rather than
  sampling `getTexture()` -- drawing routes through the renderer, which
  binds oF's built-in per-format YUV->RGB *fragment shaders* (they have
  explicit GLES2 variants), so color conversion happens on the GPU. The
  rest of the effect chain still sees a plain RGB FBO. (`getTexture()` on
  a planar frame would return only the Y plane.)

Getting that path to survive on the Pi surfaced three more latent oF
0.12.1 bugs -- unsurprising, since planar video through the generic Linux
GLES+GLFW build is yet another never-exercised-upstream combination:

1. **`ofPixels` under-allocates every planar format** (`ofPixels.cpp`'s
   `allocate()` sizes by `w*h*channels`, but I420 is 12 bits/pixel across
   three planes): the Y plane fills the whole allocation and both chroma
   planes land past its end. All the *consumers* (`getTotalBytes()`,
   `copyFrom()`, `setFromPixels()`) already size by the format-aware
   `bytesFromPixelFormat()`, so planar copies were heap-overflow writes and
   plane texture uploads read unowned memory -- an instant segfault inside
   the GL driver on the Pi, and silent garbage chroma on desktop, where the
   stray reads happen to land in mapped heap. Patched (both `allocate()`
   and `setFromExternalPixels()`) as patch 5 in `scripts/setup-pi.sh`;
   diagnosed via `gdb` + an `LD_PRELOAD` shim that touch-read every
   `glTexSubImage2D` source buffer before forwarding it to Mesa.
2. **`ofGstUtils` points its pixels into a GstBuffer it maps and then
   immediately unmaps** (`process_sample()`: `gst_buffer_map` ->
   `setFromExternalPixels` -> `gst_buffer_unmap`). Software decoders hand
   over malloc-backed buffers where the stale pointer happens to keep
   working; the Pi's `v4l2h264dec` hands over V4L2 mmap'd memory that
   genuinely goes away on unmap -> segfault on the next texture upload.
   oF ships an opt-in fix -- `ofGstVideoUtils::setCopyPixels(true)`, whose
   own doc comment cites the upstream v4l2 bug (GNOME #737427) -- which
   `ClipPlayer` enables by installing its own `ofGstVideoPlayer` backend.
3. **`ofVideoPlayer::getPlayer()` is not a query** -- it lazily creates and
   installs a default backend when none exists, so "do I already have a
   player?" can never be answered with it. `ClipPlayer` tracks backend
   installation with its own flag instead. (First attempt used
   `if(!player.getPlayer())`, which self-defeated by creating the default,
   copyPixels-off backend it was checking for.)
4. **oF kills the pipeline on v4l2's vertically-padded 1080p buffers**
   (setup-pi.sh patch 6, two parts). V4L2 decoders pad plane heights to
   macroblock alignment -- 1080 becomes 1088 rows -- so the buffer is
   bigger than the tight frame size while the row stride still equals the
   width. `ofGstUtils` treats exactly that combination as a fatal
   inconsistency and returns `GST_FLOW_ERROR`, which takes down the whole
   pipeline ("Internal data stream error" from qtdemux; symptom: solid
   green frame, position frozen at 0, duration -1). 720p only dodges it
   because 720 is already a multiple of 16. Separately, oF's fallback
   strided-I420 copy assumes each plane starts right after the previous
   plane's *visible* rows, so with padded buffers it would read chroma
   from the wrong offsets. Fixed by restricting the bail-out to
   single-plane formats and copying I420 plane-by-plane using the
   per-plane offsets and strides GStreamer actually reports.

Verified on the Pi after the fixes: all three scenes play at a measured
**1.000x real-time** (clip position advance vs. wall clock across two
screenshots), app steady at 60fps, `vqueue:src` down from 99.9% to ~9%
CPU, and correct colors on the SMPTE-style test clip (oF's conversion
shaders use plain BT.601 -- fine for this project's aesthetic). **1080p30
verified too** (also exactly 1.0x): decoder thread ~27%, copy thread ~9%,
main thread ~36%, spread across four cores -- the hardware decoder tops
out at 1920 wide (`v4l2h264dec`'s probed caps), so 1080p is the practical
ceiling and `scripts/import-clip.sh` caps there: H.264 High, yuv420p,
keyframe every second, no audio.

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

**The Pi itself currently runs `MidiControlSource`, not
`PisoundControlSource`.** Deliberate: the project doesn't want to be tied
to Pisound specifically, and a generic USB MIDI interface + USB
microphone are what's actually connected to this Pi 4 right now. Verified
working end to end: a QinHeng CH345 USB MIDI adapter (`amidi -l` /
`aconnect -l` show it as ALSA port `CH345:CH345 MIDI 1 32:0` --
`ofxMidiIn::openPort(string)` requires an **exact** match, not a substring,
so `midi.port_name` has to be the full `"client:port clientId:portId"`
string, same as the desktop config already does) and a generic USB audio
adapter (`USB Audio Device`, matched by `audio.device_name` via
`getMatchingDevices`, which -- unlike port names -- does substring
matching). Configured via a Pi-local `bin/data/config/app.local.json` (gitignored,
created directly on the Pi -- `deploy-to-pi.sh`'s rsync deliberately
excludes it, so each machine keeps its own hardware-specific overrides).
`PisoundControlSource` and its FIFO button bridge
remain unused until Pisound hardware is actually connected; the real
scene-advance button is an open gap in this configuration (no keyboard is
attached to the kiosk, and `MidiControlSource`'s button binding is
keyboard-only).

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

These three are the original CRT-decay effects from the HLD. See
`docs/videosynth-effects.md` for the planned expansion into demoscene-style
generative effects (plasma, rotozoom, color-cycling, etc.) for live
electronic music performance, including two `Scene`/`ShaderChain` scaling
changes that doc recommends making before that expansion grows much past a
handful of passes.

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
