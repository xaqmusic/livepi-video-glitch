# LivePi VideoGlitcher

A real-time **video synthesizer appliance** for live performance. A Raspberry Pi
(+ optional [Pisound](https://blokas.io/pisound/)) composites looping clips,
still images, and procedural generators through a stack of GLSL effects, driven
live by MIDI (CC + notes) and audio input, and controlled from any phone or
laptop over the box's own Wi-Fi. Flash a card, power on, connect — no desktop,
no venue Wi-Fi required.

Originally a "CRT glitch box" (see `docs/LivePi VideoGlitcher HLD.pdf`); it grew
into a full videosynth. `docs/architecture.md` is the current design of record.

## Status

**Shipping appliance.** Runs on Pi 4 hardware end to end: sealed read-only-root
golden image, per-device secrets, control-network AP + captive portal, and an
**in-app updater** (upload a bundle in the web UI — health-gated with automatic
rollback, never a reflash). Pi 5 support is designed but not yet brought up (see
`docs/tech-debt.md`). To cut a release image or an update bundle, see
**`docs/releasing.md`**.

## Features

**Compositor** — a bottom-to-top per-scene layer stack. Each layer is a looping
**clip**, a **still image** (png/jpg, transparency), or a procedural
**generator**, blended (normal / add / screen / multiply) with opacity.

- **Generators (6):** plasma, starfield, blobs, copper, fire, laser.
- **Per-layer transform:** contain-fit aspect + scale, X/Y position, horizontal/
  vertical flip, and **rotation** (continuous degrees) — all live-mappable.
- **Per-layer effects:** trails, stutter/freeze, kaleidoscope, tunnel, rotozoom,
  twister, fracture/shatter, posterize, per-layer color, and clip trim
  (start/end + ping-pong boomerang).
- **Scene-wide post effects:** h-sync tear, chromatic aberration, scanlines,
  barrel distortion, static/noise, color grade.

**Live control** — every effect parameter is a mappable target. Bind any MIDI CC
or note, or an audio band (low/mid/high), to any parameter; mappings live on the
scene so switching scenes remaps the whole surface atomically. Control source is
**auto-detected**: Pisound (audio + MIDI) → USB MIDI/audio → mock (desktop).

**Scenes & setlist** — an ordered setlist you advance by button, MIDI, or the web
UI. Per scene: an **entry transition** (fade / tear / shatter / static), an
internal **render resolution** (trade sharpness for frame rate), and optional
**timed auto-advance** (dwell `MM:SS`, then transition to the next scene) for a
self-running show. A **thermal governor** caps render resolution when the SoC
gets hot.

**Web UI** (phone/laptop) — scene editor, clip library (upload video/images with
background transcode, all-intra proxies, and image optimize), MIDI/audio
mapping with learn, network onboarding, and settings (password, audio,
performance, software update).

**Appliance** — first boot generates per-device secrets + a unique hostname
(`livepi-XXXX.local`) and stands up a control-network AP (`LivePi-XXXX`) so you
can run a whole show from a phone with no venue Wi-Fi; a captive portal joins
venue networks. Read-only root with a writable `/data` partition. An on-screen
connection card (with a scan-to-join Wi-Fi QR) appears on boot.

**Hardware** — Pisound button: quick press = next scene, ~3s hold = debug
overlay, >7s hold = setup/QR card.

## For musicians: flash & go

1. Flash `livepi-<version>.img.xz` (from a release, or built per
   `docs/releasing.md`) with Raspberry Pi Imager / balenaEtcher.
2. Power on. First boot self-provisions (partition, secrets, AP) and reboots
   once into the show.
3. Join the box's Wi-Fi `LivePi-XXXX` (or scan the QR on screen) and open the
   URL it shows. The Wi-Fi key and web password are printed on the connection
   card; you'll be asked to set your own password on first login.

## For developers: desktop build

```sh
./scripts/setup-desktop.sh   # installs openFrameworks + ofxMidi as a sibling dir
./run.sh                     # builds and launches (add -v/--verbose for logs)
```

With no hardware attached the app runs against `MockControlSource`:

| Key       | Action                             |
|-----------|------------------------------------|
| `space`   | scene button: Click (next scene)   |
| `h`       | scene button: Hold (back to first) |
| `[` / `]` | knobA down / up (bidirectional)    |
| `,` / `.` | knobB down / up (intensity)        |
| `-` / `=` | tempo down / up                    |
| `d`       | toggle debug overlay               |

The web UI runs from `frontend/` (`npm install && npm run dev`); the backend
from `backend/` (`scripts/run-backend-dev.sh`). Deploying a dev build to a
hand-provisioned Pi (rsync over SSH) is covered in `docs/deploy.md`; the
one-time SSH/network setup is at the end of that doc.

## Building & releasing

Two artifacts, one runbook — **`docs/releasing.md`**:

- **Golden image** (`scripts/build-image.sh`) — a flashable, sealed
  read-only-root `.img.xz` built on your desktop under qemu. What a new box or
  a from-scratch reflash starts from.
- **Update bundle** (`scripts/deploy-update.sh`) — an app-only
  `livepi-app-<version>.tar.zst` that ships new renderer/UI/config over the
  in-app updater. No reflash; health-gated with automatic rollback. Renderer
  (C++) changes compile in a persistent arm64 build chroot
  (`scripts/build-chroot.sh`).

## Documentation

| Doc | What |
|-----|------|
| `docs/architecture.md`     | Current design of record: render pipeline, data model, read-only root |
| `docs/distribution.md`     | Appliance design & rationale: partitions, first boot, networking, updates |
| `docs/releasing.md`        | **How to build images and update bundles** (runbook) |
| `docs/deploy.md`           | Deploying to a Pi, provisioning, button wiring, Wi-Fi onboarding |
| `docs/tech-debt.md`        | Known debt, deferred work, and limitations |
| `docs/videosynth-effects.md` / `-backend.md` / `-frontend.md` | Effects, backend, and frontend subsystem notes |
| `docs/shader-authoring.md` | Writing a new effect shader |
| `docs/clip-proxies.md`     | Clip transcode / all-intra proxy pipeline |
| `docs/pisound-hardware-notes.md` | Pisound button/overlay specifics |
| `docs/vision.md`           | Product direction |

## Repository layout

```
src/            openFrameworks renderer (C++): render/, fx/, scenes/, control/, video/, util/
backend/        FastAPI backend (livepi_backend/) — shows, clips, network, updates, settings
frontend/       React/TypeScript web UI (Vite)
scripts/        setup, provisioning, image + bundle build, updater (app-activate.sh)
systemd/        unit templates rendered by the provisioner
docs/           design + operational documentation
```
