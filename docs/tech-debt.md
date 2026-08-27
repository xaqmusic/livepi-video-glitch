# Tech debt, deferred work & known limitations

A living list. Each item notes **what**, **why it's deferred / where the risk
is**, and a pointer. Ordered roughly by impact within each section.

## Hardware / platform

- **DO NOT SHIP A BUNDLE OR IMAGE TO THE PI 4 BOX YET.** Two changes in this
  branch are unverified on Pi 4 hardware and one of them now affects every
  board: the KMS `OutputClass` (below) and the kiosk unit moving X to **vt1**
  with `-background none` for the boot splash. The only Pi 4 is with a
  collaborator and cannot be tested against; a replacement is being sourced.
  The next build targets a Pi 5 (a second team member building his own), so
  Pi 5 is the safe release path in the meantime.
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
- **BUILT: power-button gestures on a Pisound-less box** (`scripts/pi5/livepi-powerbtn.py`,
  `systemd/livepi-powerbtn.service.template`, installed by
  `provision-appliance.sh` whenever no Pisound stack is present).

  **The PMIC force-off threshold is ~5s, measured on a Pi 5 Rev 1.1**: a 4.86s
  hold survived; a slightly longer one killed the board mid-press. It is a
  HARDWARE function below Linux and cannot be vetoed. That rules out every
  hold-based gesture -- a performer holding a beat too long would drop the show
  -- so the map is TAP COUNTS and holds are deliberately inert:

  | Gesture | Action |
  |---|---|
  | 1 tap | next scene |
  | 2 taps | toggle debug overlay |
  | 3 taps | toggle setup / QR card |
  | 5 taps | password reset (recovery) |

  4 taps is an intentional gap, keeping the destructive gesture hard to reach.

  The tap window is ASYMMETRIC: 180ms after the first tap, 280ms after any
  subsequent one. The single tap is the scene advance -- the one gesture that
  happens mid-performance and the only place latency is felt -- so it resolves
  fast; once a second tap lands nobody is waiting and the extra 100ms buys a
  forgiving multi-tap. Accepted trade: a sloppy double-tap with a gap over 180ms
  reads as two scene advances rather than a debug toggle.

  Gestures are NOT reimplemented: each shells out to
  `scripts/pisound/livepi-btn.sh`, the same bridge pisound-btn uses, which
  gained an explicit `reset` action so a caller that cannot express a 30s hold
  can still reach recovery. One contract, two input sources.

  LED feedback uses **PWR (red)** only -- lit while a tap is held, one flash per
  tap on resolve, and a fast blink past 1.5s warning that the force-off is
  coming. **ACT (green)** is deliberately left on its `mmc0` trigger: SD-activity
  is a genuinely useful diagnostic and not ours to steal. Both are
  `max_brightness=1`, so blink yes, fade no.

  `logind` must be told to let go (`HandlePowerKey=ignore`, installed by the
  provisioner) or the first tap powers the box off.

- **`livepi-btn.sh` hardcodes `/data` paths for password recovery.** The reset
  gesture removes `/data/auth.json` and `/data/.claimed`, which is right on an
  appliance but wrong anywhere `LIVEPI_DATA_DIR` differs -- on a hand-provisioned
  dev box with no `/data`, auth lives at `bin/data/auth.json` and the gesture
  fires, logs, and resets nothing. Pre-existing; should read the same env the
  backend and renderer do before it ships.

## Boot & first impression

- **The boot message CANNOT be kept on screen during X's startup -- do not retry
  the VT trick.** X grabs its own VT when it starts, so the console message ends
  there and the panel is black for ~30-40s until the renderer draws. Holding the
  console in front of X was implemented and reverted: X logs "AIGLX: Suspending
  AIGLX clients for VT switch" and will NOT give a client a GL context while its
  VT is inactive, so the renderer never draws, never writes status.json, and the
  handover never fires -- a deadlock that leaves the box on a console. An early
  test appeared to prove it safe only because it switched away AFTER the renderer
  already held a context, which is a different situation.

  Covering that window needs something that draws to the FRAMEBUFFER (fbi or
  similar, /dev/fb0) before X starts -- ideally the same splash image, so the
  sequence would read as one continuous image from early boot to the show. Not
  built; the black is accepted for now.

- **X takes ~30s to initialise on the Pi 5, and it is glamor.** Measured from
  the Xorg log: a 33.5s gap between loading the glamor module and "glamor X
  acceleration enabled on V3D 7.1.10". NOT Mesa shader recompilation (the
  on-disk cache persists, 2.7MB/96 files) and only ~5s of it is demand-paging
  the 50MB libgallium off a 23.6MB/s card -- pre-reading that file saves ~5s but
  costs 8s to do, so it is a net loss done serially. The remaining ~28s is
  unexplained and is the single largest item left in boot time. It is now
  COVERED rather than fixed (the console keeps the panel during X startup), so
  it costs patience rather than looking like a dead box.

- **FIXED: `atomic_write_json` was atomic but not DURABLE.** It wrote a temp
  file and `os.replace`d it -- atomic against a concurrent reader, so nobody
  ever saw half a file -- but never fsynced, so the contents and the rename both
  sat in the page cache until ext4 committed, up to seconds later. On a normal
  machine that gap is theoretical. Here it is not: the appliance is DESIGNED to
  have its power pulled (read-only root, no clean shutdown expected) and the
  Pi 5's power button hard-cuts at ~5s. Observed for real -- a gear-menu setting
  changed moments before a power-off came back with the old value. Now fsyncs
  the file AND the containing directory. Applies to every operator-initiated
  write: settings, shows, the clip library.

- **The Pi 5 EEPROM has no "pending update" state -- do not write code that
  looks for one.** `rpi-eeprom-config --apply` commits straight into the A/B
  EEPROM's INACTIVE slot ("Force commit opposite: SUCCESS") and takes effect at
  the next boot. Until then `rpi-eeprom-config` keeps reporting the ACTIVE slot
  and `rpi-eeprom-update` says "up to date" -- so a change you just made is
  invisible to both. Reading the hardware back to confirm a write therefore
  reports the OLD value and looks like a failure. `scripts/livepi-netinstall.sh`
  always applies rather than comparing, and the backend tracks the operator's
  INTENT in settings.json, falling back to the EEPROM only for a board it has
  never set (e.g. a card moved between boards).
- **The pink/QR startup screen is the bootloader's network-install prompt**
  (`NET_INSTALL_AT_POWER_ON`), not the rainbow splash -- which is why
  `disable_splash` and `logo.nologo` have no effect on it. It is now an owner
  toggle in the gear menu (Settings ▸ Startup screen), applied through a
  deliberately narrow helper with an enumerated-verb sudoers line: it writes the
  BOARD's firmware, which survives a reflash and a card swap, so the web UI must
  never get general `rpi-eeprom-config` access. Turning it off also removes
  network-install as a recovery path for that board.

- **FIXED: the first clip load after a cold boot took ~8s and could fail
  outright.** Diagnosed on the Pi 5 (SD measured at 23.6 MB/s). It was never
  about clip size or length -- a 502s/198MB clip prerolls as fast as a 10s one,
  and warming clip DATA changed nothing (1.48s -> 1.46s, measured; that idea was
  tried and discarded). The cost is GStreamer's **one-time per-process init** --
  plugin-registry parse plus the dlopen of libav/qtdemux/h264parse -- which oF
  pays LAZILY on the first clip load. A box whose boot scene is an image or a
  generator never triggers it, so the entire bill lands on the first mid-show
  switch to a clip scene. (That is also why generator-only scenes were instant.)
  Two changes, and BOTH are needed:
  - **`setup-pi.sh` / `patch-of-video.sh` patch 7** raises oF's preroll ceiling
    from 5s to 30s. The 5s limit is a HARD FAILURE, not a slow load: `load()`
    returns false and the layer renders black. On a cold boot a single preroll
    spent 18s and still missed it. A slow card must degrade to a slow load.
  - **`ofApp::prewarmVideoStack()`** (config `render.prewarm_video`, default on)
    loads one clip during startup so the per-process cost is paid at boot
    instead of on stage. Without patch 7 this is actively HARMFUL -- the prewarm
    itself times out, burns ~18s and the renderer restarts; measured, not
    theorised.

  Verified on a cold reboot: prewarm 15.8s at boot, first switch to a clip scene
  **1.38s**, one renderer start, no failures. The cost of that trade is ~16s more
  black before first pixel -- which is precisely what the Window B splash below
  now has to cover, and why it is worth building.

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
- **BUILT: the splash is on screen for essentially the whole boot.** Verified on
  a cold Pi 5 boot; the owner's image dominates the panel from ~12s through to
  the show.
  - **Window A is BLACK.** `provision-appliance.sh` appends `quiet loglevel=3
    systemd.show_status=0 logo.nologo vt.global_cursor_default=0` to
    `cmdline.txt` and `disable_splash=1` to `config.txt`, idempotently.
  - **The splash goes on the FRAMEBUFFER**, not into X and not into plymouth.
    `scripts/livepi-bootsplash.sh` uses ffmpeg -- already a dependency -- to
    convert the configured `ui.splash_image` straight to `/dev/fb0` in the
    panel's native pixel format, contain-fit and centred exactly as the renderer
    draws it, so the handover from framebuffer to GL is not a visible jump. No
    new packages, and nothing in the initramfs (changing that on a sealed box is
    a brick path). A "LivePi is booting" spinner sits on the bottom row: a
    spinner, not a bar, because this boot has no honest total.
  - **X TAKES OVER vt1, NOT vt2, and this is the load-bearing detail.**
    `-background none` alone is not enough: on a different VT the SWITCH blanks
    the panel no matter what X does with its root window, which is why the
    splash previously vanished for X's whole ~30-40s startup. Starting X on the
    same VT the splash drew on means there is no switch and the framebuffer
    content survives. Requires `getty@tty1` disabled (the provisioner moves
    console login to tty3).
  - **The console is silenced at runtime too** (`dmesg -n 1`, `setterm --msg
    off`). `loglevel=3` alone still let a few lines scribble across the image.
    Everything still reaches the journal. Messages BEFORE the service starts
    (~12s, once the filesystem is up) are not covered -- that would need
    initramfs work or `loglevel=0`.
  - **Do not try to hold the console in front of X.** Implemented and reverted:
    X logs "AIGLX: Suspending AIGLX clients for VT switch" and will not give a
    client a GL context while its VT is inactive, so the renderer never draws,
    never writes status.json, and the handover deadlocks with the box stuck on a
    console. An early test looked fine only because it switched away AFTER the
    renderer already held a context.
  - **The owner picks the image in the gear menu** (Settings > Boot splash), from
    the still images already in the clip library. Stored as `splashImage` in
    settings.json on the WRITABLE data partition -- not app.json, whose tree is
    read-only on an appliance. The renderer and the boot script both apply the
    same precedence: settings.json first, `ui.splash_image` in app.json as the
    shipped default, so the framebuffer splash and the GL splash can never be
    different pictures. The API takes a clipId, never a free path, so the web UI
    cannot point the renderer at an arbitrary file; a stored choice is cleared
    automatically if that image is deleted.

- **The Pi 5 EEPROM has no "pending update" state -- do not write code that
  looks for one.** `rpi-eeprom-config --apply` commits straight into the A/B
  EEPROM's INACTIVE slot ("Force commit opposite: SUCCESS") and takes effect at
  the next boot. Until then `rpi-eeprom-config` keeps reporting the ACTIVE slot
  and `rpi-eeprom-update` says "up to date" -- so a change you just made is
  invisible to both. Reading the hardware back to confirm a write therefore
  reports the OLD value and looks like a failure. `scripts/livepi-netinstall.sh`
  always applies rather than comparing, and the backend tracks the operator's
  INTENT in settings.json, falling back to the EEPROM only for a board it has
  never set (e.g. a card moved between boards).
- **The pink/QR startup screen is the bootloader's network-install prompt**
  (`NET_INSTALL_AT_POWER_ON`), not the rainbow splash -- which is why
  `disable_splash` and `logo.nologo` have no effect on it. It is now an owner
  toggle in the gear menu (Settings ▸ Startup screen), applied through a
  deliberately narrow helper with an enumerated-verb sudoers line: it writes the
  BOARD's firmware, which survives a reflash and a card swap, so the web UI must
  never get general `rpi-eeprom-config` access. Turning it off also removes
  network-install as a recovery path for that board.

- **FIXED: the first clip load after a cold boot took ~8s and could fail
  outright.** Diagnosed on the Pi 5 (SD measured at 23.6 MB/s). It was never
  about clip size or length -- a 502s/198MB clip prerolls as fast as a 10s one,
  and warming clip DATA changed nothing (1.48s -> 1.46s, measured; that idea was
  tried and discarded). The cost is GStreamer's **one-time per-process init** --
  plugin-registry parse plus the dlopen of libav/qtdemux/h264parse -- which oF
  pays LAZILY on the first clip load. A box whose boot scene is an image or a
  generator never triggers it, so the entire bill lands on the first mid-show
  switch to a clip scene. (That is also why generator-only scenes were instant.)
  Two changes, and BOTH are needed:
  - **`setup-pi.sh` / `patch-of-video.sh` patch 7** raises oF's preroll ceiling
    from 5s to 30s. The 5s limit is a HARD FAILURE, not a slow load: `load()`
    returns false and the layer renders black. On a cold boot a single preroll
    spent 18s and still missed it. A slow card must degrade to a slow load.
  - **`ofApp::prewarmVideoStack()`** (config `render.prewarm_video`, default on)
    loads one clip during startup so the per-process cost is paid at boot
    instead of on stage. Without patch 7 this is actively HARMFUL -- the prewarm
    itself times out, burns ~18s and the renderer restarts; measured, not
    theorised.

  Verified on a cold reboot: prewarm 15.8s at boot, first switch to a clip scene
  **1.38s**, one renderer start, no failures. The cost of that trade is ~16s more
  black before first pixel -- which is precisely what the Window B splash below
  now has to cover, and why it is worth building.

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
- **BUILT: boot splash, two windows, two owners.** Verified on a cold Pi 5 boot.
  - **Window A (firmware -> kernel -> systemd -> X) is BLACK.**
    `provision-appliance.sh` appends `quiet loglevel=3 systemd.show_status=0
    logo.nologo vt.global_cursor_default=0` to `cmdline.txt` (idempotently, one
    option at a time, so a hand-tuned box keeps its own) and `disable_splash=1`
    to `config.txt`. `console=tty1` is KEPT on purpose -- the progress bar needs
    a console, and X takes vt2 so the two never contend.
  - **A progress bar covers it** (`scripts/livepi-bootsplash.sh`,
    `livepi-bootsplash.service`). ~40s of pure black is indistinguishable from a
    dead box, so it draws an ASCII bar on tty1 advancing on REAL milestones
    (local-fs -> backend -> Xorg -> renderer -> first frame), creeping gently
    between them so it never looks wedged. Deliberately not plymouth: those
    assets live on the read-only root and the early splash is baked into the
    initramfs, so changing it means `update-initramfs` as root -- a genuine
    brick path on a sealed box.
  - **Window B is the owner's image**, drawn by the renderer
    (`SceneRenderer::setSplash`, config `ui.splash_image` /
    `ui.splash_min_seconds`). Contain-fit and centred so it suits a 1920x720 or
    a 1080p panel, alpha composited over black. It lives on the DATA dir, so it
    survives app updates and is replaceable through the same path clips use.
  - **Ordering is load-bearing.** The splash must be on the output FBO BEFORE
    `prewarmVideoStack()` blocks the main thread, or the ~16s cold-boot prewarm
    is black and the splash appears only after the wait it exists to cover.
  - The hold has two conditions, not one: a minimum floor (default 2s) so a
    warm boot doesn't flash it, AND `layersReady()` so a slow boot keeps it up
    until there is genuinely something to replace it with. Measured cold: splash
    up at +0.3s after the GL context, released 16.7s later.

  Remaining: the splash image is chosen by config, not yet pickable in the web
  UI -- the clip library already holds images, so a "use as splash" action there
  is the obvious next step.

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
