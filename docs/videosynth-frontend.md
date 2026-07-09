# Videosynth frontend design doc

## North star

Ease of use. A solo performer already has enough to manage live -- the
setup phase (building scenes, mapping controls) is where this UI needs to
be effortless, so the performance itself needs nothing from the browser
except an occasional quick tweak. Everything below optimizes for that:
setup on a laptop/tablet before a show, minimal touch-friendly fallback
during one.

This is explicitly a first pass, not a final design -- per the backend
doc's own philosophy, stand this up with clean-but-default styling (a dark
theme, both the obvious modern choice and genuinely practical for a screen
used backstage in low light) and revisit visual polish once real scenes
have been built and real sets have been played. The two modes and the
`MappableControl` pattern below are the parts worth getting right early;
color/spacing/exact layout are exactly the parts expected to change once
this "hits hands."

## Two modes, not one responsive view

**Edit mode** (laptop/tablet, pre-show): the full show designer -- show
library, setlist editor, scene editor, clip library. Information-dense is
fine here; this is a sit-down task.

**Live mode** (phone, mid-set): deliberately minimal. Current scene name,
big touch targets for whatever parameters that scene has MIDI-mapped (the
ones that matter live), and software next/back buttons mirroring the
Pisound button's Click/Hold as a fallback. Not a place to reorder layers
or browse clips -- if something needs that level of change, that's an
Edit-mode task, done between songs or after the set.

Building these as two distinct views rather than one layout that reflows
is a deliberate choice: a stressed performer mid-set shouldn't be looking
at a cut-down version of the complex editor, they should be looking at a
screen built only for "what does this scene expose, and how do I nudge it
quickly."

## Edit mode, screen by screen

**1. Show library** -- saved setlists (`GET /api/shows`), pick one active,
duplicate/rename. The entry point; nothing else matters until a show is
open.

**2. Setlist editor** -- the ordered scene list Click cycles through live.
Up/down buttons as the *primary* reorder control, not drag-and-drop --
dragging a list item is finicky on a touchscreen, and reordering is a core
task here, not an edge case. Drag-and-drop can exist as a bonus desktop
affordance but shouldn't be the only way to do the most common thing on
this screen.

**3. Scene editor** -- the core screen, and the one worth the most care:

- **Layer stack**, top of list = foreground (matches "foreground and
  background" directly). Each layer row: thumbnail (clip) or icon
  (generator), a kind toggle, blend mode + opacity, and an expandable
  section for that layer's own effects (rotozoom, kaleidoscope, etc.).
- **Post-effects section** -- the scene-wide CRT-decay chain plus anything
  meant to hit the whole composited frame.
- **Every parameter control is the same component** (`MappableControl`,
  below) -- a slider/dropdown/toggle with a small MIDI icon next to it.
  One consistent gesture everywhere is the actual answer to "learn must be
  available for every parameter": it's not a special screen, it's not
  something to remember how to do differently per control type, it's the
  same click-and-wiggle everywhere.
- **Mappings tab** -- lists every CC currently bound in this scene and
  what it fans out to ("CC 74 -> H-sync Intensity, Layer 2 Opacity"),
  resolved to layer/param *names*, never raw IDs or indices. Lets a
  performer audit a scene at a glance instead of re-discovering bindings
  by hovering every slider -- directly serves the ease-of-use goal, and
  it's the natural place to show the "this scene is heavy" performance
  indicator from the backend doc's telemetry channel.

**4. Clip library** -- a grid, shared across every scene (any layer can
reference any clip). Upload is one combined dropzone-plus-Browse-button
component: drag-and-drop for the desktop file-browser case, an explicit
Browse button opening the OS file picker for the phone/tablet case (where
drag-and-drop from a file manager isn't really a gesture that exists).
Same upload endpoint either way -- the two entry points are purely a
frontend affordance choice, not two different backend paths.

## The `MappableControl` pattern

Every parameter in the app -- layer opacity, an effect's frequency knob,
post-effect intensity, all of them -- renders through one shared component:

```
[ slider/dropdown/toggle ]  [MIDI icon]
```

Clicking the MIDI icon:
1. Opens (or reuses) the telemetry WebSocket from the backend doc.
2. Shows a "listening..." state on that control specifically.
3. Binds the first CC it sees to this control, updates the in-memory
   (unsaved) show, shows "Mapped to CC 74."
4. If a mapping already exists on this control, clicking again re-arms
   Learn (re-mapping) rather than requiring an explicit unmap step first --
   the common case during setup is "try a different knob," not "remove
   the mapping and separately add a new one."

Building this once as a shared component (not per-effect-type bespoke UI)
is what actually makes "MIDI learn available for every parameter" true in
practice rather than aspirational -- a new effect added to the catalog
gets Learn for free just by using the same control component, no extra
frontend work per effect.

## Mobile responsiveness

Edit mode should still be *usable* on a tablet (the setlist/scene editor
reflow to single-column below some breakpoint), but isn't optimized for a
phone -- that's what Live mode is for. Concretely: Edit mode targets
laptop/tablet widths, Live mode is phone-first and is a genuinely separate
route/view, not a CSS breakpoint of the same page. Don't build one
component tree trying to serve both; the interaction needs are different
enough (touch-drag-reorder vs. big single-purpose sliders) that sharing
layout code would fight both use cases at once.

## Open questions

- **Learn-mode ambiguity with 14-bit CC pairs** -- per the backend doc's
  caveat, a controller sending an MSB/LSB pair might have Learn grab the
  "wrong half." UI should make re-learning a single click (see
  `MappableControl` above) so this is a minor annoyance to correct, not a
  blocking bug, rather than trying to detect 14-bit pairs automatically.
- **How much clip metadata to show in the library grid** -- duration and
  a thumbnail are the obvious minimum; whether resolution/file size/upload
  date earn a place in the grid vs. a detail view is a "see how it feels
  once there are 30 clips in there" question, not one to answer blind now.
- **Scene duplication UX** -- duplicating a scene (start from something
  close rather than from scratch) seems clearly valuable given how much
  setup is on each scene (layers, mappings, post-effects), but where that
  action lives (setlist editor row menu? a button inside the scene editor
  itself?) is worth deciding once the setlist editor has an actual visual
  pass, not before.
