# Videosynth backend & show-data design doc

## Vision

`docs/videosynth-effects.md` catalogs the effects; this doc covers how a
performer actually *programs* a set of them ahead of a show. The goal:
log into the Pi from a browser on the same network, build a setlist --
ordered scenes, each with a layered stack of clips/generators and their own
effect parameters -- then unplug the laptop and have the Pi boot straight
into that setlist. Live control during the set stays exactly what's
already built (MIDI clock/CC, the scene button's Click/Hold via
`SceneManager`); the web UI's job is authoring content beforehand, not
running the show in real time. See `docs/videosynth-frontend.md` for how
that authoring UI is actually organized on screen.

## Architecture: two processes

The openFrameworks app keeps doing what it's good at -- real-time
rendering, MIDI/audio/button input -- and stays unaware that a browser
exists. A separate, small backend service owns the show *data* and the web
UI. This mirrors how every other non-rendering concern in this project is
already handled outside the C++ codebase (deploy is bash+rsync, the
Pisound button is a shell script + FIFO) -- the render engine's job stays
narrow, and a Pi 4 has cores to spare for a lightweight service that's
mostly idle once a show is actually running.

```
Browser (laptop/phone) --HTTP--> Backend service (Pi)
                                    - REST API (scenes, clips, effects)
                                    - owns show.json + a library of shows
                                    - serves the built UI + clip thumbnails
                                            |
                                            | writes (atomically)
                                            v
                                  bin/data/config/show.json
                                            |
                                            | polls mtime, reloads on change
                                            v
                                  openFrameworks app (existing renderer)
```

**No new live IPC.** The backend writes `show.json` on save; the oF app
checks its mtime once a frame (or once a second -- negligible either way)
and reloads scene data when it changes. This was the deliberate call from
the earlier discussion: you're standing at the rig with its monitor
visible while programming a set, so sub-second reload latency costs
nothing, and it avoids building a bidirectional live-preview protocol
(OSC/WebSocket) and the "committed vs. mid-edit" state tracking that comes
with it. Worth revisiting only if hands-on use of the editor actually feels
laggy -- not something to guess at up front.

**Backend language:** Python + FastAPI by default (fast to stand up a JSON
CRUD API + file uploads + auto-generated API docs; trivial Pi 4 resource
cost). Not a load-bearing decision -- swap for Node/Express if you'd rather
keep the whole stack in JS, nothing else here depends on the choice.

## Relationship to existing config

`app.json`/`app.local.json` keep their current job: **device/hardware
config** -- which control source, MIDI port, window size. `show.json` is
new and holds **creative content** -- the setlist itself. Concretely, the
`"scenes"` array currently embedded in `app.json` moves out into
`show.json`'s richer model; `app.json` stops knowing about scenes at all.
Clean separation: `app.json` answers "how is this Pi wired up," `show.json`
answers "what does this Pi play."

Given a touring performer plausibly wants different setlists for different
gigs (not just one setlist forever), `show.json` is really a **library of
named shows plus a pointer to the active one**, not a single flat file --
cheap to support from day one, so this doc assumes:

```
bin/data/shows/
  ├─ active.json          # { "activeShow": "warehouse-set-2026" }
  ├─ warehouse-set-2026.json
  └─ basement-set.json
```

The oF app only ever reads whichever file `active.json` points at; the UI
is where you'd manage the library and flip which one is active.

## Data model

```
Show
 ├─ schemaVersion: 1
 └─ scenes: [Scene]              // ordered -- this is the setlist,
                                  // Click/Hold already cycles through it

Scene
 ├─ id                           // stable, assigned once, never reused
 ├─ name
 ├─ layers: [Layer]              // ordered bottom-to-top, any count
 ├─ mappings: [Mapping]          // CC or audio-band triggers -- see
                                  // "Modulation mapping & learn" below
 └─ postEffects: { param map }   // applied to the final composited frame
                                  // (h-sync tear, chromatic aberration,
                                  // stutter, barrel/fisheye, ...)

Layer
 ├─ id                           // stable, assigned once, never reused
 ├─ kind: "clip" | "generator"
 ├─ source: clipId  OR  "plasma" | "starfield" | "fire" | ...
 ├─ blendMode: "normal" | "add" | "screen" | "multiply"
 ├─ opacity: 0..1
 ├─ layerEffects: { param map }  // warps scoped to just this layer
 │                                // (rotozoom, kaleidoscope, tunnel, ...)
 └─ params: { }                  // generator-specific, if kind == generator

Clip
 ├─ id                           // stable -- what layers reference,
 │                                // never the raw filename
 ├─ path: "clips/foo.mp4"
 └─ (duration/resolution, filled in on upload)
```

`params`/`layerEffects`/`postEffects` are all the same generic
string-keyed float map from `videosynth-effects.md`'s architecture
section -- one representation, used at three different scopes.

**Every cross-reference is by stable ID, never by array position or raw
path.** A layer's `source` for a clip is a `clipId`, not a filename --
renaming or re-uploading a clip doesn't orphan every scene using it. A
mapping target names a `layerId`, not `layers[1]` -- reordering the layer
stack (which the setlist/scene editor exists specifically to make easy)
doesn't silently repoint every mapping at the wrong thing. This is the
single most important rule in this data model: positional/path references
look fine until the first reorder or rename, then break silently with no
error, well after real setlists exist to break.

`schemaVersion` costs nothing to add now and is the only way a future
`Scene`/`Layer` shape change can detect and migrate old saved shows instead
of just failing to load them.

## How this maps onto the render engine

Nothing here replaces `ShaderPass`/`ShaderChain` -- it wraps them:

1. **Generators are `ShaderPass`es that never sample `srcTex`.** A "plasma"
   layer is the same interface as `ChromaticAberrationPass`, just a shader
   that computes its own color from scratch. No new base class needed.
2. **Each layer renders through its own `ShaderChain`** (its
   `layerEffects`) before compositing -- `ClipPlayer`'s frame or a
   generator's output goes through that layer's warp passes exactly like
   the existing single-clip pipeline does today, just scoped to one layer
   instead of the whole scene.
3. **A new `LayerCompositor`** blends the per-layer results together in
   order via blend mode + opacity -- one shader with a `blendMode` int
   uniform switching formula (normal/add/screen/multiply) is plenty; no
   need for four separate compiled programs.
4. **The composited result feeds the existing scene-level `ShaderChain`**
   for post effects -- this is exactly the `ShaderChain` that already
   exists today, just now fed a composited multi-layer frame instead of a
   single clip's frame.

Pipeline, end to end: **per-layer render → per-layer effect chain →
composite in order → scene-level post chain → output.** This is the same
layer/master-effects split real VJ tools (Resolume, TouchDesigner) use --
a reassuring sign it's a proven shape, not a novel risk.

### Effect/generator parameter schema

For the UI to build controls without hand-coding a form per effect (slider
for a float, dropdown for an enum, etc.), the backend needs to know each
effect's parameter shape. Simplest v1: a small hand-maintained manifest
(JSON or a static table) mapping effect name to its params --

```json
{
  "plasma": {
    "opacity": {"type": "float", "min": 0, "max": 1, "default": 0.4},
    "frequency": {"type": "float", "min": 0.5, "max": 8, "default": 2}
  },
  "rotozoom": {
    "rotationSpeed": {"type": "float", "min": -2, "max": 2, "default": 0.3},
    "zoomAmount": {"type": "float", "min": 0, "max": 1, "default": 0.2}
  }
}
```

served via `GET /api/effects`. This is the pragmatic starting point --
hand-maintaining one manifest entry per effect is fine at a dozen effects.
If that gets tedious, the natural upgrade is having each `ShaderPass`
expose its own schema via a virtual method and generating the manifest
from the real passes instead of a hand-kept file, but that's a refinement
to reach for only once the manifest is visibly annoying to keep in sync,
not something to build preemptively.

## Modulation mapping & learn

Today `knobA`/`knobB` are two fixed, global CC assignments configured once
in `app.local.json`. That doesn't cover the actual requirement: the same
physical knob (e.g. a synth's filter cutoff) needs to drive *different*
parameters -- possibly several at once -- depending on which scene is
active, and switching scenes needs to swap that mapping table
instantaneously, with no audible/visible seam. The same generalized
mechanism also has to cover the other modulation source this project
supports: the music itself, split into low/mid/high bands (see "Audio-band
modulation" below) -- a mapping's trigger is either a MIDI event or an
audio band, resolved by the same per-frame step either way.

```
Mapping
 ├─ trigger: { type: "cc", number: 74 }   // "type" kept explicit, not
 │                                         // implicitly CC-only -- your
 │                                         // collaborator's synth almost
 │                                         // certainly also sends
 │                                         // aftertouch/mod wheel/etc., and
 │                                         // this is also where "type:
 │                                         // audioBand" (below) plugs in,
 │                                         // instead of a parallel system
 └─ targets: [
      { layerId: <id>, param: "opacity", min: 0, max: 0.6 },
      { param: "postEffects.hsync.intensity", min: 0, max: 1 }
    ]
```

### Audio-band modulation

We don't need FFT-grade analysis for this -- just enough to feel the beat.
The C++ renderer splits the incoming audio into three bands with a pair of
Linkwitz-Riley crossovers (100 Hz and 2000 Hz -- low/mid/high), takes the
abs value of each band's signal, and runs it through a one-pole EMA
(attack/release can differ so hits punch in fast and decay a little
slower, like a compressor). Each band's smoothed envelope lands in
`ControlState` right alongside `audioLevel` (`lowBand`/`midBand`/
`highBand`, each 0..1) at a reasonable tick rate -- no need to ship raw
audio anywhere outside the renderer process.

A mapping's trigger for this is `{ type: "audioBand", band: "low" | "mid" |
"high" }`; its targets use the same `{ param, min, max }` shape as a CC
mapping -- from the target's point of view, "how much of this band" and
"how much of this knob" are the same resolution step, just reading a
different live 0..1 input. This is also exactly the "for any param, pick a
band and a percentage" control the performer-facing side needs -- `min`/
`max` *is* the percentage-of-range dial, nothing extra to design there.

**Combination semantics** matter once a target can be touched by more than
one thing at once (a CC mapping *and* an audio-band mapping on the same
param, or an audio-band mapping on top of a scene's static baseline
value). CC mappings are **absolute** -- the resolved value *is* the
target's value for that frame, full stop, matching how a hardware knob
works. Audio-band mappings are **additive** -- the band's contribution
(`band * (max - min)`, so `min`/`max` size how much of the band touches
the param) is summed on top of whatever the param's value already is that
frame (the scene's static baseline, or a CC mapping's resolved value),
then clamped to the parameter's valid 0..1 domain -- NOT to `[min, max]`,
which would cap a param below its own baseline whenever the baseline sits
above `max` (found while implementing). That's the "layers right on top of
whatever's already happening to that parameter" behavior described in
`vision.md` -- a performer dials in a subtle bass pulse without it fighting
or overriding a CC knob they're also riding on the same parameter.

Mappings live on the `Scene`, not on the device. `ControlState.ccValues`
(raw CC number -> normalized value) already exists in the renderer and is
already generic -- it's just never been used for anything but the two
hardcoded knobs. The missing piece is a per-frame resolution step that
reads *the active scene's* mapping table and writes each target's resolved
value into that scene's live param maps.

Because scene switching is already atomic (`SceneManager` just changes
`currentIndex`), **remapping is seamless for free** once mappings are
scene-scoped data -- the frame after a Click, the resolver is already
reading the new scene's table. One real, non-bug consequence worth knowing
about: a target snaps to wherever the physical knob is *currently* sitting
the moment its scene's mapping takes over, not wherever the previous
scene's mapped value was. That's normal MIDI-mapped behavior (the same
thing happens on any hardware synth patch change), not something to
engineer around.

`knobA`/`knobB` become redundant once this lands -- they're just the
degenerate case of mapping the same CC to the same target in every scene.
Recommend deprecating them rather than running two parallel mapping
mechanisms once the general system exists.

**Learn mode is the one exception to "no live protocol."** Clicking Learn
next to a parameter and turning a knob needs to see that CC number *right
now* -- a save/reload round-trip doesn't cover it. Kept minimal and
one-directional so it doesn't reopen the live-preview question:

- The oF app writes the last incoming CC (`{cc, value, timestamp}`) to a
  small status file whenever a CC message arrives.
- The backend watches that file and relays it over a WebSocket, live only
  while a client has a Learn control armed.
- Clicking Learn opens that connection, shows "listening...", binds the
  first CC it sees to the target parameter (updating the in-memory,
  unsaved show document), and closes.

This means mapping setup happens against the real gear, live -- no
guessing CC numbers, no possibility of typing the wrong one. Same channel
is a natural place to also carry live frame time/FPS from the renderer
(see "performance headroom" below) -- one small piece of infrastructure
covering two needs.

**Performance headroom.** Nothing stops a scene from accumulating enough
layers and per-layer effects that it doesn't hit frame rate on the Pi's
GLES driver, and the first anyone would find out is live on stage. Since
the telemetry file above already exists for Learn mode, having the oF app
also write its current frame time into it and surfacing a simple "this
scene is heavy" indicator in the scene editor is nearly free -- worth
doing before scenes get built blind against an unknown performance ceiling.

## Backend features

- **Show/scene CRUD** -- whole-document model: `GET /api/shows/:name`
  returns the full show, `PUT /api/shows/:name` replaces it. A show is a
  small, cohesively-edited document (you're dragging scenes around and
  tweaking layers in the same sitting), not a large multi-user dataset, so
  fine-grained per-scene endpoints would be more machinery than the problem
  needs. Reordering scenes, duplicating one, adjusting a layer's opacity --
  all just edits to the in-memory document, one `PUT` on save.
- **Show library** -- `GET /api/shows` lists saved shows;
  `POST /api/shows/active` sets which one the oF app loads on boot.
- **Clip library** -- `GET /api/clips` lists `bin/data/clips/*` with
  duration/resolution/a thumbnail URL; `POST /api/clips` accepts a
  multipart upload and generates its thumbnail server-side
  (`ffmpeg -ss ... -frames:v 1`).
- **Effect/generator catalog** -- `GET /api/effects`, the manifest above.
- **Live telemetry** -- `WS /ws/telemetry`, active only while a client is
  connected (Learn mode armed, or the editor open). Streams last-seen CC
  and current renderer frame time, relayed from the oF app's status file.
- **Auth** -- a shared password behind a session cookie. This is LAN-only,
  single-user gear, not a multi-tenant service -- sized to match, not
  enterprise auth, unless you want it reachable outside your own network.
- **Validation on write** -- before persisting a show, the backend should
  check every clip path exists, every generator name is in the effect
  catalog, and every param value is in its declared range. A bad manual
  edit (or a bug in the UI) should get rejected at save time, not crash
  the renderer on next reload.
- **Atomic writes** -- write to a temp file and rename over the target, so
  the oF app's mtime-poll never catches a half-written `show.json` mid-save.

## Sequencing

No change from the earlier decision: `SceneManager`'s existing Click
(advance) / Hold (jump to scene 1) is the whole playback engine. The web
UI's only job is authoring the ordered scene list Click cycles through --
no timer, no auto-advance state machine.

## Open questions

- **Clip transcoding** -- if an uploaded clip is in a format/codec oF's
  GStreamer backend doesn't like, should the backend auto-transcode via
  ffmpeg on upload, or just reject with a clear error? Leaning toward
  reject-with-a-clear-error for v1 (transcoding adds real complexity --
  format detection, progress reporting for a possibly-slow operation on
  Pi hardware) and revisit if it turns out to be a constant friction point.
- **Preview stills** -- is a static thumbnail per scene (rendered how,
  given the renderer only exists as the live oF process) worth building,
  or does "look at the Pi's own monitor while editing" cover it? Assumed
  the latter for now per the earlier live-preview decision; flagging in
  case scene thumbnails in the show-library view turn out to matter for
  navigating a long setlist.
- **Multi-Pi shows** -- out of scope entirely for now (this assumes one Pi,
  one screen), noting only so it doesn't get accidentally designed in or
  out later without the choice being deliberate.
- **Venue networks** -- a shared-password gate is proportionate on a home
  LAN, but a touring rig plugged into unknown venue WiFi is a different
  threat model (anyone on that network could reach the editor). Worth
  revisiting before the first gig on a network you don't control -- not
  something to solve today.
