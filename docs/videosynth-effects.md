# Videosynth effects design doc

## Vision

The original HLD (`docs/LivePi VideoGlitcher HLD.pdf`) scopes a CRT glitch
box: prerecorded CRT-filmed clips, "vandalized" by three signal-decay
effects (h-sync tear, chromatic aberration, stutter/freeze), synced to MIDI
clock and modulated by CC/audio. That's built and confirmed working
(`docs/architecture.md`).

This doc expands the scope: the same engine, aimed at live electronic music
performance rather than a single-purpose appliance. The three existing
effects simulate a CRT *failing*. The effects catalogued here are early-90s
demoscene generators (Amiga/PC demoscene, pre-3D-accelerator era) --
mathematically cheap, visually loud, and a different flavor entirely:
*generative*, not decay. Composited with real CRT-filmed footage, the two
aesthetics should reinforce each other -- the demoscene look already reads
as "90s computer," and the footage's own CRT texture sells it as physical
rather than digital-clean.

Because this now targets a live music set rather than a single art
installation, audio/BPM reactivity is a first-class requirement for every
effect below, not an afterthought -- a VJ set needs to sustain visual
interest and hit accents over an hour+, not just decorate a loop.

## How these fit the existing architecture

Every effect below is still just a `ShaderPass` (`src/fx/ShaderPass.h`):
`setup()` loads a shader, `apply(src, dst, controlState, scene)` binds
uniforms and draws. Nothing here needs a new base abstraction --
compositing a generative pattern *with* the source video is just fragment
shader math (`mix()`/add/screen the procedural color with `texture(srcTex,
uv)`), the same shape `ChromaticAberrationPass` already uses. Follow
`docs/shader-authoring.md`'s existing "Adding a new pass" steps for each of
these.

Two real scaling problems show up once there are ~10 passes instead of 3,
covered in "Architecture implications" below -- worth reading before
starting to build these, since retrofitting them after 8 passes exist is
more work than designing for it from pass #4 on.

## Effect catalog

Four categories. Within each, effects are listed in the order I'd build
them (cheapest + most distinct from what exists first).

### Coordinate-warp passes

Resample the source texture through a distorted lookup instead of drawing
something new -- same shape as `HSyncTearPass`, just a different warp
function. These are the safest bets: guaranteed cheap (one texture fetch per
pixel, same as existing passes), and they read as "glitch" rather than
"generated content," so they sit naturally alongside the existing three.

- **Rotozoom** -- rotate + scale the sample coordinates around center:
  `uv' = rot(angle) * (uv - 0.5) / scale + 0.5`. `angle`/`scale` driven by
  time and knobs. At subtle settings, a woozy wobble; cranked up, the
  classic Second Reality spinning background. Needs mirror-repeat (not
  clamp) at the texture edges or corners go black when zoomed out.
  Suggested controls: rotation speed (knobB), zoom depth (audio level),
  optional scale-snap on beat.

- **Twister bars** -- vertical-strip sibling of `HSyncTearPass`: offset each
  *column* by `noise(uv.x * bands + time)` instead of each scanline. Can
  literally reuse `common.glslinc`'s `noise()`. Same beat-spike-and-decay
  shape as h-sync tear's `beatSpike`.

- **Tunnel / vortex** -- polar remap: `r = length(uv - center); a =
  atan(uv.y - center.y, uv.x - center.x); uv' = vec2(fract(1.0/r +
  time*speed), a / TWO_PI)`. Using the *video* as the tunnel-wall texture
  (rather than a generated brick pattern, the demoscene original) reads as
  the footage getting sucked into a drain. Strong as a scene-transition:
  ramp `speed` from 0 to a spike and cut to the next scene when the tunnel
  "swallows" the frame.

- **Kaleidoscope** -- fold angle into N wedges: `a = abs(mod(a, TWO_PI /
  segments) - PI / segments)`, then reproject to Cartesian and sample.
  `segments` as an integer-quantized knob (3-12) is an obvious, satisfying
  live control -- turn one knob, get a completely different symmetry.

- **Barrel / fisheye (lens sim)** -- `uv' = uv + (uv*2-1) * dot(uv*2-1,
  uv*2-1) * amount`, plus an optional edge vignette. Unlike the others,
  this isn't a demoscene trick -- it's simulating the *physical monitor*,
  so it belongs last in the chain (everything else happens "on the tube,"
  this is the tube itself), and `amount` is more a per-scene/global
  constant (which monitor is this simulating) than something to modulate
  live, maybe with a subtle audio-reactive "breathing" added on top.

### Palette / color-cycling passes

The actual defining Amiga trick: color-cycling got "animation" without
redrawing anything, by rotating palette indices. On modern hardware we
don't need the indexed-color constraint, but the *aesthetic* -- hard color
steps snapping through a fixed ramp -- is exactly the "90s computer" tell
that a pure decay effect can't produce.

- **Posterize + cycling LUT** -- quantize source luminance to N bins
  (`bin = floor(luminance * N) / N`), map each bin through a small
  procedural color ramp (the Inigo Quilez cosine-palette trick: `color =
  a + b * cos(TWO_PI * (c * t + d))` is a cheap, tunable way to get a
  ramp without a texture lookup). Two cycling modes worth exposing: smooth
  continuous drift (lava-lamp feel) vs. a hard step once per beat (using
  `controlState.beatInBar` changing as the trigger, matching
  `HSyncTearPass`'s beat-edge detection) -- the beat-synced version is the
  one that'll read as distinctly "sequenced," not just "colorful."
  Controls: bin count (coarser = more retro), cycle speed or beat-sync
  toggle, a small set of hardcoded palette coefficient presets (lava,
  vaporwave, monochrome-phosphor-green) selectable via CC.

- **Copper bars** -- named for the Amiga's Copper coprocessor, which raced
  the beam to change colors mid-scanline for free. `bar = sin(uv.y * freq +
  time * speed)` through a color ramp, then either scanline-tint
  (`srcColor * mix(1, barColor, amount)`) or screen-blended on top. Doesn't
  need to touch the video's own content to read as "80s computer," which
  makes it a useful contrast beat against the CRT-decay effects. Also
  works as a wipe transition: sweep the bar position top-to-bottom once,
  cutting to the next scene as it passes.

### Generative overlay composites

Computed independently of the source frame, then blended on top (additive/
screen/`mix`, not a coordinate warp). These are the ones that most read as
"separate layer" rather than "damaged video," so opacity is the single most
important control on all three -- too much and the footage disappears
entirely, which may sometimes be the point (a full-send moment) but
shouldn't be the default state.

- **Plasma** -- sum of sines, the quintessential demo effect: `p =
  sin(x*a + t) + sin(y*b + t*1.3) + sin((x+y)*c + t*0.7) +
  sin(length(uv - 0.5)*d - t*1.7)`, mapped through the same cosine-palette
  function as the color-cycling pass (share the code). Blend at low-to-
  moderate opacity so the video stays legible underneath. Controls:
  opacity on audio level (louder = more plasma bleed-through), frequency
  coefficients on knobs for how busy the pattern reads.

- **Starfield** -- recommend a per-pixel hashed-grid approach (stars
  computed from a hash of `floor(uv * density)`, no persistent state)
  over a true CPU-tracked point field -- it's a fragment-shader-only
  `ShaderPass` like everything else, no new C++ state, at some cost to
  how convincingly "3D" the depth/parallax reads compared to a real
  point-sprite starfield. Worth trying the cheap version first and only
  reaching for a real point-mesh pass if it doesn't sell the effect.
  Controls: density/speed on audio level, a speed burst on beat
  ("hyperspace jump").

- **Fire** (Doom-fire palette-diffusion) -- the most architecturally
  distinct effect here: it's a **feedback** effect, seeding heat at the
  bottom row and diffusing/cooling it upward each frame, which means it
  needs to read its own previous output the way `StutterBufferPass`
  already reads its ring buffer -- not a stateless per-pixel function like
  everything else above. Recommend a dedicated low-resolution simulation
  FBO (the original ran at ~320x200; no reason to simulate at full output
  resolution when it's getting blended/keyed in afterward). Composite via
  luma-key (fire reveals the video through itself) or bottom-edge matte.
  Controls: base heat / flame height on audio level. Build this last --
  it's the one genuinely new piece of machinery (a persistent sim buffer),
  not just a new fragment shader.

## Architecture implications

Two things about the current design won't scale gracefully past ~5-6
passes. Neither blocks building the first few effects above, but both are
worth deciding before the `Scene` struct and `ofApp::setup()`'s pass list
grow much further, since retrofitting later means touching every existing
pass.

**Superseded by `docs/videosynth-backend.md`:** that doc's browser-based
show designer needs this same param-map generalization plus a full
layered-scene model (foreground/background/N layers, each with its own
effect chain, composited before the scene-level post chain below) --
written up in detail there rather than duplicated here. The param-map
recommendation immediately below is still the right first step either way.

**1. `Scene`'s fixed-field-per-effect struct.** Today:

```cpp
struct Scene {
    std::string name;
    std::string clipPath;
    float hSyncIntensity = 0.5f;
    float chromaticIntensity = 0.5f;
    bool stutterEnabled = true;
};
```

One field per effect was fine for three effects; it's not fine for twelve.
Recommend replacing the fixed fields with a generic parameter map (`std::
map<std::string, float> params`, e.g. keyed `"hsync.intensity"`,
`"plasma.opacity"`, `"fire.enabled"`), with each pass reading its own
namespaced keys via a small `scene.getParam(key, default)` helper. This is
a mechanical change to `Scene.h`/`Config.cpp`'s JSON parsing and each
existing pass's `apply()`, not a redesign -- `stutterEnabled`'s existing
"read a scene-configured flag, gate internally" pattern is already the
right shape, it just needs to generalize past three hardcoded struct
fields.

**2. `ShaderChain`'s fixed, always-on pass list.** Today `ofApp::setup()`
constructs all passes once and `ShaderChain::process()` runs every one of
them, every frame, regardless of scene (`StutterBufferPass` just gates
its *effect* on `scene.stutterEnabled` internally, but still pays a full
FBO ping-pong + draw call every frame either way). That's fine at three
passes; at twelve, most scenes only want three to five of them active, and
paying the ping-pong cost for the other seven every frame is real wasted
bandwidth on a Pi's GLES driver.

Don't over-build this preemptively -- the cheap first step, and probably
sufficient on its own, is making each disabled pass's `apply()` skip
straight to an early passthrough draw (skip the procedural math, still pay
one draw call) rather than a redesign. If that's empirically still too
slow once several effects exist (profile on real Pi 4 hardware, don't
guess), the next step up is a name-keyed pass *registry* in `ShaderChain`
plus a per-scene ordered list of which pass names are active, so
`process()` only iterates the scenes' actual subset. Worth prototyping
that second step only once the first one is measured and found wanting.

**Performance budget, generally:** every pass here is one full-screen
fragment shader pass on the Pi's GLES driver. That's cheap per-pass by
demoscene-era standards, but it adds up across a chain -- keep an eye on
frame time on real Pi 4 hardware (`ofGetFrameRate()`/a debug-overlay
counter) as passes are added, and treat "how many effects can reasonably
be active in one scene" as an empirical question to answer on hardware,
not a number to guess up front.

## Suggested build order

1. **Scene param-map refactor** (architecture implication #1 above) --
   do this before adding a fourth effect, not after; every effect below
   needs a place to put its per-scene parameters, and retrofitting the
   fixed-struct fields once four or five more effects exist is strictly
   more work than doing it now.
2. **Rotozoom** and **posterize + cycling LUT** -- cheapest, most distinct
   from the existing three, good smoke test for the param-map refactor.
3. **Kaleidoscope**, **copper bars**, **twister bars** -- same shape as
   above, fill out the warp/palette categories.
4. **Plasma**, **starfield** -- first generative overlays; establishes the
   opacity-blend pattern the rest of that category shares.
5. **Tunnel/vortex**, **barrel/fisheye** -- barrel specifically last since
   it wants to sit at the very end of the chain once there's more to
   simulate "the tube" over.
6. **Fire** -- last; the only effect needing new feedback-buffer
   machinery rather than reusing the existing per-pixel `ShaderPass`
   pattern.
7. Revisit `ShaderChain`'s always-on pass list (architecture implication
   #2) once real Pi frame-time data says it's needed.

## Open questions

- Per-scene active-effect *count* isn't decided -- do scenes hand-pick 3-5
  effects each (curated, more like the current model), or does everything
  stay always-on with intensities dialed to zero as the default "off"
  state? Affects both the param-map design and the live-performance UX
  (how many knobs is a performer actually touching per scene).
- Palette presets (lava/vaporwave/phosphor-green/etc.) -- worth a shared
  `bin/data/shaders/palette.glslinc` included by every pass that uses the
  cosine-palette trick, so a preset picked for one effect (say, color-
  cycling) can stay consistent with plasma's palette in the same scene.
- Fire's simulation-resolution vs. output-resolution mismatch needs an
  upscale/blend strategy (bilinear resize read back into the main chain)
  -- not hard, just needs deciding before that pass gets built.
