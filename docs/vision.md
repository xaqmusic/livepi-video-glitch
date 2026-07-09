# The video rig — where this is headed

This is a living document. Nothing here is locked in — it's here so you
can see the shape of where this is going, push back on anything, and drop
in ideas whenever one hits you. We'll keep updating it as the thing itself
changes.

## What this actually is

A visual instrument that plays alongside you, not a video that plays at
you. It runs on its own small box — no laptop needed at the show, no
operator riding faders in the corner. You plug it in, it already knows the
whole set, and from there it's reacting to the same hands you're already
using to play.

The look is CRT footage, genuinely filmed off a tube monitor, getting
actively vandalized in real time — plus a second flavor of visuals pulled
from early-90s computer demos, the kind of thing Amiga and PC demoscene
kids were doing with almost no processing power at all. Two aesthetics
that shouldn't clash but do complement each other: one is analog decay,
the other is digital and generative, and both read as unmistakably "old
computer" in a way that sits right next to electronic music.

## The look

**The glitches** — these live on top of your actual footage, damaging it
in ways that feel physical:

- **Tearing** — the picture rips sideways, scanlines snapping like a VHS
  tape losing tracking, hitting hardest right on the beat.
- **Color bleed** — red and blue drift apart at the edges like a signal
  losing sync, more of it the louder things get.
- **Stutter/freeze** — the frame catches and loops for an instant, a
  skipped beat, a stuck record.

**The generators** — pulled from the demoscene, built from scratch on
top of (or blended into) your footage rather than just damaging it:

- **Plasma** — liquid, shifting color washing across the screen, the
  classic lava-lamp-made-of-light look.
- **Rotozoom** — the picture spins and breathes around its own center,
  hypnotic, straight out of a 1993 demo intro.
- **Tunnel** — pulled through the footage like it's the walls of a
  wormhole made from your own video.
- **Kaleidoscope** — folds into mirrored symmetry; turn one knob, get a
  completely different pattern.
- **Color cycling** — the palette itself steps or pulses through colors
  on the beat, no new footage needed, just color doing the work — the
  actual trick 90s demos used to fake animation for free.
- **Copper bars** — bands of color sweeping the screen, named for the old
  Amiga trick of racing the beam; reads as pure 80s/90s computer, a nice
  contrast against the CRT grit.
- **Starfield** — points streaking past, a hyperspace jump on cue.
- **Fire** — flames licking up from the bottom of the frame, built from
  the same algorithm that made Doom's title screen burn.
- **A subtle screen curve** — the picture genuinely bulging like it's
  sitting on real curved glass, not a signal effect but the "monitor"
  itself.

None of this is fixed to a single mood — tearing and color-bleed hit
different against a plasma wash than they do against straight CRT
footage, and part of what's fun here is finding which combinations feel
right for which song.

## How it plays live

Each song (or moment in a set) gets its own **scene** — a look built from
one or two layers (say, a background clip and a generator effect riding
on top of it) plus whichever glitches are dialed in. Building the set
ahead of time is a lot like programming a lighting rig, or laying out a
synth's patch memory: you decide what scene one looks like, what scene
two looks like, all the way through, in order.

The part that matters most for how you actually play: **the same knob can
mean something different in every scene.** Your filter knob might warp
color in one song, spin the picture in the next, and drive three things
at once in the one after that — but all of that is decided ahead of time,
not something you're managing live. When the scene changes (a press on
the Pisound button, or a key, whatever we land on), the new mappings are
just *there*, instantly, on the next frame. You're not doing anything
different with your hands — the box just starts listening for something
new.

Mapping a knob is meant to be nearly invisible as a task: click "learn"
next to whatever you want to control, turn the actual knob on your gear,
and it's bound. It's watching your real instrument as you set it up, not
asking you to remember or type a CC number.

## The sound itself can drive it too

Knobs aren't the only hands on the controls — the music can be one too.
Split into low, mid, and high (bass, mids, and top end), the actual sound
coming out of the speakers becomes another control source: a kick drum
nudging the plasma, a hi-hat flickering through the color cycling, a
bassline breathing into the tunnel's pull. Dial in how much of a band
touches a parameter — a little for a subtle pulse under everything else,
a lot for something that rides the beat hard — and it layers right on top
of whatever's already happening to that parameter, whether that's a fixed
setting or something a knob is already driving live.

It means the rig isn't only reacting to your hands. It's reacting to the
room.

## Building a set together

All of this setup happens from a browser — log in from a laptop or phone
on the same network as the box, build out scenes, drop in clips, map
knobs, all without touching any code. The idea is that you can sit down
with your own gear connected, build a scene, hear how a knob feels
against it, adjust, and move on — the same kind of iterative process as
programming a synth patch, just for pictures instead of sound.

Clips live in one shared library — anything you've dropped in is
available to any scene, drag a file in from your desktop or pick one from
your phone's camera roll.

## This is just the shape of it

Everything above is where things stand right now, not where they end.
You'll have better ideas about which effects suit which songs, what a
transition between scenes should feel like, what's missing entirely — all
of that belongs here as it comes up. Think of this less as a spec and more
as the first pass at describing a shared vocabulary for talking about the
rig as it gets built.
