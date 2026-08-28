# Contributing to LivePi VideoGlitcher

Thanks for taking an interest. This is an open appliance: the software is MIT
licensed, the image is a free download, and the whole thing is meant to be
opened up and messed with. Fork it, rip out the effects you don't like, put
your own name on your build.

## The short version

- **License** — everything is [MIT](LICENSE). By sending a patch you agree it
  ships under MIT too (see [Sign-off](#sign-off) below).
- **No hardware needed to contribute.** The renderer, the backend, and the web
  UI all run on a Linux desktop. `./run.sh` starts the renderer and the backend
  together with mock MIDI, no Pi and no Pisound attached.
- **Before opening a PR**, make sure the renderer compiles on the desktop
  (`make -j`) and the frontend builds (`npm run build` in `frontend/`). Those
  two are the whole gate for most changes.

## Getting set up

You need a checkout of [openFrameworks](https://openframeworks.cc) 0.12.x
alongside this repo. `scripts/setup-desktop.sh` handles the desktop side;
`scripts/setup-pi.sh` is the Raspberry Pi equivalent and also applies the half
dozen oF patches a Pi needs (documented inline — they're all upstream bugs, all
idempotent).

```bash
./run.sh              # renderer + backend, mock MIDI, web UI on :8080
./run.sh --verbose    # with HTTP access log and per-CC MIDI readout
cd frontend && npm install && npm run dev   # UI with hot reload
```

`bin/data/config/app.local.json` (gitignored) overrides the checked-in
`app.json` — point it at a real MIDI device or a different scene list without
dirtying the repo.

## Where things live

| Path | What |
|---|---|
| `src/` | openFrameworks renderer (C++) — `render/ fx/ scenes/ control/ video/ util/` |
| `backend/livepi_backend/` | FastAPI backend — shows, clips, network, update, settings |
| `frontend/` | React/TypeScript web UI (Vite) |
| `scripts/` | setup, provisioning, image + bundle build, updater |
| `docs/` | design docs — `architecture.md` is the design of record |

`docs/architecture.md` is worth reading before a substantial change.
`docs/shader-authoring.md` is the fast path if you just want to add an effect.

## Adding an effect

This is the most common contribution and it's deliberately cheap. A new
per-layer effect or transform parameter is **one line** in
`backend/effects_manifest.json` — that manifest drives the UI, the validation,
and the MIDI mapping, and the renderer reads it generically. Write the shader,
add the manifest entry, done. `docs/shader-authoring.md` walks through it.

## Things worth knowing

A few constraints that aren't obvious from the code, and that a patch can
accidentally violate:

- **GLES 2.0 is the target**, uniformly, on every Pi generation. Shaders must
  compile there — no `#version 150`, no desktop-GL-only builtins. The desktop
  build uses the same path so you'll usually catch it locally.
- **Pi 4 vs Pi 5 is a runtime tier, never a compile-time one.** One arm64 build
  runs on both. Branch off `livepi::platformInfo()` or `hardware.detect()`, and
  keep the tier *advisory* — it must not change what a show means, or a
  Pi-5-authored show stops playing on a Pi 4.
- **Scene-level fields** (transition, renderScale, autoAdvance) have to
  round-trip through three places: `ShowLoader`, the backend `Scene` model, and
  the TypeScript type. Miss one and shows silently lose the field on save.
- **The box ships sealed** (read-only root, app on `/data`). Anything that
  writes to disk at runtime needs to write under `/data`, not next to the
  binary.

## Sign-off

This project uses the [Developer Certificate of Origin](https://developercertificate.org/)
rather than a CLA. It's one line — no paperwork, no rights assignment, nothing
signed away beyond confirming you have the right to contribute what you're
contributing.

Add a `Signed-off-by` trailer to each commit:

```bash
git commit -s -m "your message"
```

which produces:

```
Signed-off-by: Your Name <your.email@example.com>
```

That's it. Your contribution stays yours; it just ships under the project's MIT
license along with everything else.

## Pull requests

- Branch off `main`. Small, focused PRs land fastest.
- Verify `make -j` and `npm run build` before pushing.
- Say what you tested on. "Desktop only" is a completely fine answer — say it
  rather than leaving it implied, since a maintainer with hardware can check the
  Pi side.
- Match the surrounding code. This codebase comments the *why* — the
  non-obvious constraint, the bug that forced the shape — rather than restating
  the *what*. Patches that keep that up are much easier to merge.

## Reporting bugs

Include the box's software version (Settings ▸ Software update shows it), the
Pi model, and whether you're on a sealed image or a dev checkout. If an update
bundle failed to apply, `/data/app/logs/apply.log` survives reboots and is the
thing to attach — journald on the box is volatile and will have thrown away the
interesting part.

## Trademark

The code is MIT and you can do whatever you want with it, including building and
selling boxes. The **names** "LivePi" and "VideoGlitcher", and the project's
logo, aren't part of that license grant — ship your fork under your own name.
It costs you nothing and it keeps support requests pointed at whoever actually
built the box.
