# Tech debt, deferred work & known limitations

A living list. Each item notes **what**, **why it's deferred / where the risk
is**, and a pointer. Ordered roughly by impact within each section.

## Hardware / platform

- **Pi 5 not brought up on hardware.** Groundwork has landed -- a runtime
  hardware tier (`src/util/Platform.h` + `backend/livepi_backend/hardware.py`,
  advisory only), Pisound-optional provisioning (`LIVEPI_PISOUND`), and audio
  input that survives a generic USB interface. What remains needs a real Pi 5
  on the bench: which decode element GStreamer autoplugs and whether playback
  holds 1.000x real-time, that the GL context comes up on Mesa **V3D 7.1**, and
  a re-measured layer/decode budget (the tradeoff inverts -- the Pi 4 offloads
  decode to VideoCore, the Pi 5 spends A76 cores on software H.264). The code
  re-read says `ClipPlayer` most likely needs **no change** for software
  decode; see `docs/distribution.md` "Hardware support: Pi 4 and Pi 5".
- **`setup-pi.sh` patch 6 only handles padded I420, not padded NV12.** The
  plane-offset copy is keyed on `OF_PIXELS_I420`; a padded NV12 buffer falls
  through to `setFromAlignedPixels(..., stride[0])`, which places the chroma
  plane by tight packing and gets it wrong. Invisible today (both the Pi 4's
  `v4l2h264dec` and the desktop path land on I420/unpadded NV12), but it is
  what the Pi 5's hardware HEVC decoder would hand back -- so generalizing the
  patch to N planes is the prerequisite for decoding HEVC natively on a Pi 5
  instead of transcoding to H.264 at ingest.
- **No physical button on a Pisound-less box.** The Pisound button's three
  gestures (next scene / debug overlay / setup card) have no source on a Pi 5.
  Scene switching survives via the gear menu's MIDI note/CC binding; the
  overlay and card are web-UI-only there. The Pi 5's PMIC power button emits
  `KEY_POWER` and could be reclaimed (`HandlePowerKey=ignore` + an evdev
  reader) -- designed, not built.

## Updates & distribution

- **GitHub Releases pull channel — deferred.** The activation engine
  (`scripts/app-activate.sh`) is source-agnostic; only web-UI **upload** is wired
  today. The pull adapter (`GET .../releases/latest` → asset download, config
  `update.github_repo` / `update.channel`) is designed but not built. Offline
  venues keep the upload path regardless. See `docs/distribution.md` "Updates"
  and `project_updater_design`.
- **A/B root (kernel/OS updates) — deferred.** Bundles update the *app* only.
  Kernel, apt packages, systemd units, and the overlay config still require a
  reflash. A RAUC/SWUpdate A/B-root track is earmarked to lift this.
- **Signed-bundle verification — not implemented.** The updater verifies a
  **sha256**; a detached signature checked against a public key baked into the
  image is designed but not built. Until then, trust is "you built the bundle."
- **`app-activate.sh` doesn't re-own the extracted tree (belt-and-suspenders).**
  The perms bug that made a chroot-built bundle unreadable by the `pi` services
  is fixed **bundle-side** (`build-bundle.sh` `chmod -R a+rX`). The on-device
  activator could additionally `chown/chmod` the extracted `versions/<v>` dir so
  *any* bundle activates regardless of how it was packed. Landing it needs a
  reflash (the activator is baked into the factory image).
- **Bundle manifest `gitHash: "unknown"`.** Bundles built in the chroot report
  no git hash (the chroot excludes `.git/`). Cosmetic — the `--version` label is
  the real identity — but a build stamp would help support.

## Networking

- **Improv BLE onboarding — deferred to v2.** iOS Safari lacks Web Bluetooth;
  the captive portal (`captive.py` + NetworkManager) is the onboarding path.
- **No shared `livepi.local` alias.** Boxes are reached by their unique
  `livepi-XXXX.local`; a stable shared alias was left to the AP gateway.

## Renderer & features

- **Rotation contain-fit is not recomputed for non-90° angles.** A rotated layer
  keeps the *unrotated* contain-fit rectangle, so at non-axis angles it can crop
  at the corners or show gaps at the edges — use the layer `scale` param to fill.
  A future refinement could recompute the fit for the rotated bounding box, or
  add a snap-to-90 mode that swaps width/height. `src/render/SceneRenderer.cpp`.
- **Auto-advance dwell counts from scene-active, transition included.** A "30s"
  scene advances 30s after you land on it, *including* its ~sub-second entry
  transition. Deterministic and simple (no coupling to renderer transition
  state); to exclude the transition, add `transition.duration` to the threshold
  in `SceneManager::update`.
- **`SoftwareUpdate.tsx` device-code block still mentions `ssh`/`sudo`.** Kept
  because it surfaces the device/hotspot code to the owner, but the wording is
  mildly technical for a pure end-user. (The dev "build a bundle…" line was
  removed.)
- **Stale comments in `src/scenes/Scene.h`.** Generators are described as a
  "black placeholder until the demoscene generator passes exist" and
  `layerEffects`/`params` are marked "(future)" — both exist now.

## Testing

- **No automated test suite.** The renderer, backend, and updater are validated
  by manual + hardware testing (the updater's core logic was exercised locally
  with stubbed `systemctl`/`systemd-run`/`curl`). Rotation and auto-advance were
  verified by compile + the end-to-end flash/update test, visually confirmed —
  but there are no unit/integration/regression tests. This is the biggest
  structural gap.

## Housekeeping / operational

- **No git tags.** Image and bundle versions are ad-hoc `--version` labels;
  `git describe` yields a bare hash. Tagging releases (`v1.0.0`, `v1.2.0`) would
  make `build-image.sh` / `build-bundle.sh` self-version and give a clean history.
- **Working-tree dev state shows as modified.** `bin/data/clips/library.json`
  and `bin/data/shows/active.json` carry local dev clips/show and show up dirty
  in `git status`. Harmless (they never ship — `/data` seeds fresh), but noisy;
  consider gitignoring the mutable data or committing clean defaults.
- **Stale `versions/1.1.0` on the first dev box.** The failed pre-perms-fix
  bundle left a `~8MB` `root:root 700` dir under `/data/app/versions/` on the
  original test box. Harmless clutter; needs root to remove. (`app-activate.sh`
  doesn't prune old versions — a future GC could cap retained versions.)
