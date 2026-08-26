# Tech debt, deferred work & known limitations

A living list. Each item notes **what**, **why it's deferred / where the risk
is**, and a pointer. Ordered roughly by impact within each section.

## Hardware / platform

- **Pi 5 X11 kiosk needs an explicit KMS OutputClass; UNVERIFIED on Pi 4.**
  `provision-appliance.sh` now writes `/etc/X11/xorg.conf.d/99-livepi-kms.conf`
  unconditionally (matched on the `vc4` DRIVER name, not a `/dev/dri/cardN`
  number, since card numbering isn't stable across Pi generations). Verified
  necessary and sufficient on the Pi 5 -- without it Xorg grabs fbdev as the
  primary screen and dies before the renderer gets a context. It *should* be a
  no-op on the Pi 4 (Xorg already autoconfigures modesetting there), but that
  has not been re-verified on Pi 4 hardware. **Sanity-boot a Pi 4 kiosk before
  shipping the next Pi 4 image.**
- **Pi 5 bring-up: DONE on hardware 2026-08-26** (Pi 5 Model B Rev 1.1, 2GB).
  Verified: GL comes up as V3D 7.1.10.2 / GLES 3.1 / Mesa 26.2.0 with the GLES 2
  targeting unchanged; `avdec_h264` autoplugs and negotiates I420 with **zero**
  `ClipPlayer` changes, at 0.99x clock-synced; 2 x 1080p layers hold the 30fps
  floor (3 x fails); the hardware tier reads `pi5` from both the renderer and
  the backend; and generic USB MIDI + audio drive mappings with no Pisound
  present. Measurements in `docs/architecture.md`. **Residual caveats, not
  blockers:**
  - The layer budget was measured on a **1920x720** panel (~50% cheaper in fill
    than 1080p) on the **2GB** variant -- indicative, not publishable. Re-measure
    on a 1080p panel before quoting numbers to anyone.
  - `openAudioInput`'s fallback ladder and `downmixToMono` are **unexercised**:
    the test interface accepted mono @ 128 frames first try. A capture-stereo-only
    device would prove them.
- **`setup-pi.sh` patch 6 only handles padded I420, not padded NV12.** The
  plane-offset copy is keyed on `OF_PIXELS_I420`; a padded NV12 buffer falls
  through to `setFromAlignedPixels(..., stride[0])`, which places the chroma
  plane by tight packing and gets it wrong. Invisible today (both the Pi 4's
  `v4l2h264dec` and the desktop path land on I420/unpadded NV12), but it is
  what the Pi 5's hardware HEVC decoder would hand back -- so generalizing the
  patch to N planes is the prerequisite for decoding HEVC natively on a Pi 5
  instead of transcoding to H.264 at ingest.
- **No physical button on a Pisound-less box** -- designed against real Pi 5
  hardware, not built. The Pisound button's three gestures (next scene / debug
  overlay / setup card) have no source on a Pi 5. Scene switching survives via
  the gear menu's MIDI note/CC binding; the overlay and card are web-UI-only.

  Verified on a Pi 5 Rev 1.1, so the design rests on facts rather than guesses:
  - The power button is a plain `gpio-keys` **evdev** device (`EV=3`, so it
    reports key down *and* up -- hold duration is just press->release). Bind it
    by the stable path `/dev/input/by-path/platform-pwr_button-event`, NOT
    `event0`; numbering shifts.
  - `logind` owns it today (`HandlePowerKey=poweroff`, the default). Reclaiming
    it needs `HandlePowerKey=ignore` in a `logind.conf.d` drop-in.
  - The **`PWR` LED is free** (trigger `[none]`) and supports the kernel
    `timer` trigger, so the Pisound's blink-while-held feedback is
    `delay_on`/`delay_off` writes -- no userspace blink loop. Use `PWR`, not
    `ACT`: `ACT` is on the `mmc0` trigger and taking it costs SD-activity
    diagnostics. `max_brightness=1` on both, so blink yes, fade no.
  - A daemon should **shell out to `scripts/pisound/livepi-btn.sh`** rather than
    reimplement the map -- that script already derives its action from its own
    invocation name and owns the >=30s password-recovery gesture. One gesture
    contract, two input sources.

  **The open risk that decides the design:** the Pi 5's PMIC does a *hardware*
  forced power-off on a sustained hold, below Linux entirely, and the threshold
  is unmeasured (deliberately not tested over SSH -- it would kill the box). If
  it fires at ~5s then `HOLD_3S` (debug) survives but the >7s card gesture and
  the >=30s password reset are **physically impossible**. Time it with a
  stopwatch before building anything hold-based. If confirmed, remap the long
  holds to **click counts** on the Pi 5 (double = debug, triple = card) --
  immune to the PMIC, same FIFO verbs, and `pisound.conf` already proves the
  map is data rather than code.

  Reclaiming the button also removes clean shutdown from it. That matters less
  on a read-only-root box (pulling power is safe by design -- the point of the
  overlay), but the web UI should grow a shutdown control regardless.

## Boot & first impression

Measured on the Pi 5 bring-up box (2GB, Pi OS Lite Trixie), 2026-08-26.

- **The kiosk is gated on `network.target` for no reason -- biggest single win
  on time-to-first-pixel.** `systemd/livepi-video-glitch.service.template` has
  `After=sound.target network.target data.mount`, and `network.target` doesn't
  go active until **@16.9s**. The renderer needs no network to draw a splash or
  play a clip (`NetInfo` is refreshed at runtime for the connection card, not at
  startup). Dropping `network.target` from that `After=` should let the kiosk
  start as soon as `data.mount` + `sound.target` are ready.
- **~13s of the 23s userspace boot is fat an appliance doesn't need.**
  `systemd-analyze` said `5.2s (kernel) + 23.2s (userspace) = 28.4s`, with
  `cloud-init` sitting *in the critical chain* (Pi OS ships it for headless
  provisioning -- a job `firstboot.sh` already owns here). Candidates to mask in
  `provision-appliance.sh`: `NetworkManager-wait-online` (6.0s -- blocks on
  connectivity a venue may not have), `e2scrub_reap` (2.6s),
  `rpi-eeprom-update` (2.1s, or make it occasional), `cloud-init-main` (2.0s),
  `man-db` (1.6s), `apt-daily` + `apt-daily-upgrade` (1.6s, actively hostile on
  a sealed box).
- **Boot splash -- two windows, two owners, only one user-updatable.** Agreed
  design; not built.
  - **Window A (firmware -> kernel -> systemd -> X up): make it BLACK, not
    branded.** `cmdline.txt` currently carries `console=tty1`, which is what
    puts scrolling text on the panel. Move it to `console=tty3` and add `quiet
    loglevel=3 logo.nologo vt.global_cursor_default=0`, plus `disable_splash=1`
    in `config.txt` for the firmware rainbow. Free, no packages, no initramfs
    coupling.
  - **Plymouth is out for USER content.** Theme assets live on the read-only
    root, and the early splash is baked into the **initramfs** (hence
    `plymouth-set-default-theme -R`). Updating it on a sealed box means
    `update-initramfs` as root -- one of the few ways to genuinely brick this
    thing, which the updater's "can't brick" guarantee forbids. `/boot/firmware`
    *is* writable, so a file could be staged there behind a narrow sudoers
    helper, but the initramfs rebuild makes it pointless. A *fixed* brand mark
    baked into the image is fine; a customer logo is not.
  - **Window B (renderer has a GL context -> first scene decoded): the
    user-updatable PNG, and it is nearly free.** `SceneRenderer::setup()`
    already clears `outputFbo` to opaque black, and `render()` deliberately
    leaves `outputFbo` untouched until `layersReady()` -- the existing
    freeze-frame-instead-of-black-flash logic. Drawing a PNG into that FBO
    instead of clearing it means the splash holds until the first scene has
    actually decoded, with no new state machine. Source it from `/data`
    (e.g. `/data/branding/splash.png`) so it rides the same writable-partition
    + web-upload path clips already use; seed a default in `dataprep.sh`;
    fall back to black if missing.
  - **Window B needs a MINIMUM HOLD or it is invisible.** Measured on this box:
    service start -> GL context 684ms -> clip decoded 807ms. **Window B is
    ~0.8s** -- a flash, and perversely the faster the box the less the logo
    shows. Add `splash.min_seconds` (default ~2-3s, zero-able): hold until
    `layersReady()` *or* the floor, whichever is later. The floor deliberately
    delays the show a couple of seconds, which is the right call for an
    appliance.
  - **Sequencing matters.** A 3s logo in front of 25s of black is the wrong
    order of work -- do the two boot items above FIRST, re-measure power-on to
    first pixel with a real reboot, then size the splash against the real
    number.

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

- **RESOLVED: the renderer now reads `effects_manifest.json`** via
  `src/util/ParamDomains.*`, so mapping contributions clamp to each param's real
  declared range instead of an invented one. Kept below for the history, since
  the failure mode it describes is instructive.

  ~~**The renderer has no access to the param domains in `effects_manifest.json`.**~~
  The manifest is the single source of truth for every param's `min`/`max`, but
  it lives backend-side and the renderer never reads it -- so any renderer-side
  code needing a param's valid range has to either invent one or do without.
  That gap already caused one real bug: `MappingResolver::resolve()`'s audio-band
  contribution clamped to a hardcoded `0..1`, which was correct when every param
  was 0..1 and silently wrong once signed params arrived. An audio-mapped
  `transform.x`/`.y` had its negative baseline stamped back to 0 every frame, so
  the layer refused to translate left or up while positive values worked
  perfectly -- it read as a UI bug and was nothing of the sort. Fixed by guarding
  against the baseline and the mapping's own endpoints instead of an invented
  domain. The durable fix is to make the manifest readable by the renderer
  (ShowLoader could load it alongside the show) so domains are asserted from one
  place, per CLAUDE.md's "adding a param is one line in the manifest" promise.

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
