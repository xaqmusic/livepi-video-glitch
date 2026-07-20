# Distribution & provisioning

How LivePi reaches a musician who has never opened a terminal -- flashing a
card (or buying a pre-built box), getting it onto WiFi at a venue with only a
phone, keeping it from corrupting when someone yanks the power, and updating it
without a re-flash. This is the *consumer* story; `docs/deploy.md` stays the
*developer* rsync-deploy how-to, and this doc reuses everything it already
documents (the kiosk unit, `setup-pi.sh`, the OS choice).

This is a fully open-source project, so GPL components (ffmpeg built with x264)
are a non-issue -- we ship source and binaries freely.

## North star

A musician should be able to: **flash a card (or plug in a pre-built box),
power it on, connect a phone, and be running a show in under five minutes --
with no keyboard, no terminal, and no dependence on the venue's WiFi.** Every
decision below serves that sentence. The web UI is the entire product surface;
the musician never sees Linux.

## What's missing today

The current path is developer-only and not shippable as-is:

- **No install path** but `git clone` + `./scripts/setup-pi.sh` + manual kiosk
  wiring (`docs/deploy.md`) -- a terminal session per box.
- **No network provisioning at all** -- WiFi is assumed pre-configured out of
  band (Raspberry Pi Imager or by hand). Confirmed: nothing in the repo touches
  WiFi, hostname, or mDNS.
- **Shared insecure secrets** -- every build has the same `LIVEPI_PASSWORD`
  (`"livepi"`) and `LIVEPI_SECRET_KEY` (`"livepi-dev-secret-change-me"`) from
  `backend/livepi_backend/config.py`. Fine for one dev Pi, unacceptable across a
  fleet.
- **Writable root** -- a power-yank mid-write corrupts the SD card. This is the
  single most common way Pi appliances die in the field, and musicians unplug
  things.
- **Updates mean a re-flash or SSH** -- no in-product update.

The good news: the app already draws a clean **app-vs-data boundary**
(`.rsyncfilter`), which is exactly the line a read-only-root appliance needs.

## Distribution model

**Ship a pre-built SD-card image, not an install script.** An install script
means the musician flashes stock Raspberry Pi OS, gets it on WiFi, opens a
terminal, runs a command, and waits while openFrameworks + the app compile on
the Pi (slow and fragile). Every step is a support ticket. A golden image
collapses all of it into "flash the card, plug it in, it works" -- how every
consumer Pi product ships (OctoPi, Volumio, RetroPie, Pi-hole's image).

- **Format** -- a compressed `livepi-vX.Y.Z.img.xz`, flashed with Raspberry Pi
  Imager or balenaEtcher. Published on **GitHub Releases** (free hosting, signed
  artifacts, release notes).
- **Built, not hand-rolled** -- use **CustomPiOS** (the framework OctoPi and
  MotionEyeOS are built on -- literally "an app distro on top of Raspberry Pi
  OS") or `pi-gen`. The image's provisioning stage *is* `scripts/setup-pi.sh`
  plus the manual kiosk steps `docs/deploy.md` currently lists by hand (see
  below). So the image is reproducible from what already exists.
- **Pre-flashed hardware is the most turnkey channel.** Given the Pisound HAT
  dependency, "buy this box" (Pi + Pisound + case + card, assembled and imaged)
  beats "buy these parts and flash this card" for both the customer and the
  margins. The image download stays free for tinkerers; the box is the product.

The image must **bake what is manual today** (`docs/deploy.md` "Autostart at
boot"): the X11 packages Raspberry Pi OS Lite doesn't ship
(`xinit xserver-xorg xserver-xorg-legacy x11-xserver-utils`), the
`/etc/X11/Xwrapper.config` `allowed_users=anybody` edit, the rendered systemd
units (`livepi-video-glitch.service`, `livepi-backend.service`), the oF build,
and the Pisound driver (Blokas apt). None of that should ever be a step a
customer performs.

## Hardware support: Pi 4 and Pi 5

Support **both from day one** -- the Pi 5 is the more capable box and it makes
no sense to ship a product that ignores it. Aim for **one image** (both are
64-bit Trixie, same `aarch64` userland) that **adapts at runtime** by detecting
the Pi model and the decoders actually present, rather than two separate images
to maintain.

Distribution-level Pi 5 notes:

- **Pisound v1.2** for the Pi 5 (Active-Cooler clearance, I2S-master/EEPROM
  fixes) -- see `docs/pisound-hardware-notes.md`. v1.1 is the Pi 4 baseline.
- **Power and cooling** -- the Pi 5 wants a **5V/5A USB-C PD** supply and
  **active cooling**; the pre-flashed-box channel should offer a Pi 5 variant
  with both.

The renderer needs real work before the Pi 5 is *verified*, and that work is
graphics/decode-engine territory. **These are TODOs for Pi 5 bring-up, not
solved here:**

- **No hardware H.264 decoder on the Pi 5.** The Pi 5 dropped the VideoCore
  H.264 decode block entirely -- it has hardware **HEVC** decode only. The whole
  `ClipPlayer` pipeline is tuned for the Pi 4's `v4l2h264dec`: copy-pixels mode
  for the V4L2 mmap that vanishes on unmap, and the I420 stride/offset handling
  for the 1080->1088 macroblock padding. On the Pi 5, GStreamer will fall back to
  a **software H.264 decoder** (`avdec_h264`; the Cortex-A76 at 2.4 GHz decodes
  1080p easily). **TODO:** verify and branch the decode path by model -- the
  negotiated pixel format, whether copy-pixels is still needed, and the fact that
  the Pi-4-only stride/padding workarounds don't apply to a software decoder.
- **GL/GLES on the Pi 5's Mesa V3D driver.** The kiosk is GLES2 via `startx`
  (GLFW has no KMS/DRM path -- see `docs/architecture.md`), and the six oF GLES
  source patches in `setup-pi.sh` should port unchanged. **TODO:** verify the GL
  context comes up on V3D and adjust the kiosk/unit if it differs.
- **Re-measure the layer/decode budget.** The tradeoff inverts: the Pi 4
  offloads decode to VideoCore, the Pi 5 spends A76 cores on software decode --
  even though the Pi 5 is faster overall. `docs/architecture.md`'s decode-budget
  tables are Pi-4-measured and shouldn't be assumed on the Pi 5.

**Pi 5 upsides to capture once it's supported:**

- **Native reverse** -- software decoders play rate `-1` fine, so the Pi 4's
  boomerang-bake workaround (built because `v4l2h264dec` stalls on reverse)
  becomes optional on the Pi 5.
- **Keep HEVC** -- with hardware HEVC decode, ingest could stop transcoding
  phone HEVC down to H.264 on the Pi 5 and decode it directly.
- **Headroom** -- more simultaneous layers, higher resolution.

## Filesystem & partitions

**Read-only root, writable data.** An unclean shutdown (a yanked power cable)
must never corrupt anything the box needs to boot. Root is mounted read-only via
`overlayroot` (writes go to a tmpfs overlay that evaporates on reboot); the only
writable persistent storage is a separate `/data` partition holding user
content, which is journaled and written through the atomic-rename paths the
backend already uses.

| Partition | FS | Mount | Mode | Holds |
|---|---|---|---|---|
| `boot` | FAT32 | `/boot/firmware` | read-only (rw only during an OS update) | kernel, firmware, `config.txt`, `cmdline.txt` |
| `rootfs` | ext4 | `/` | **read-only** (overlayroot) | OS + the LivePi app: binary, backend, `frontend/dist`, oF libs |
| `data` | ext4 | `/data` | read-write, journaled | all user content + per-device secrets + network config |

This maps directly onto the existing `.rsyncfilter` app-vs-data boundary:

- **On `/data`** (the "Pi-authored user data" the filter already protects):
  `shows/`, `clips/library.json`, `clips/.thumbs/`, `clips/.pingpong/`, full-res
  footage, `auth.json`, plus the per-device `backend/.env` and the network
  config. Point the backend at it with **`LIVEPI_DATA_DIR=/data`** (already an
  env knob in `config.py`).
- **On the read-only root**: everything else -- the compiled binary, backend
  code, `frontend/dist`, shaders, `config/app.json`.
- **Runtime scratch** already lives in the right place: the IPC status file and
  command FIFO are in `/tmp/livepi/` (tmpfs -- fine on a read-only root), and
  transcode temp files / thumbnails / boomerang bakes write under `clips/`,
  i.e. onto `/data`.
- **Logs** go to the journal (volatile) so nothing hammers the card.

The image ships **three** partitions: boot, rootfs, and a small trailing
`LIVEPI_DATA` seed planted right after rootfs by the build. That trailing
partition **boxes rootfs in** so no first-boot resizer can grow rootfs into the
card's free space; instead the first boot **grows the `LIVEPI_DATA` partition to
fill the card** (`growpart` + `resize2fs`). This is more robust than the earlier
"append a partition to unclaimed free space" approach, which depended on having
disabled *every* stock rootfs-auto-grow path (and one slipped through, giving a
7 MB `/data`).

### How it's implemented

> **Status: built (pending on-hardware validation).** The app is verified
> RO-root-clean, the data split is wired, and the image build + the first-boot
> chain that flips `/` read-only are implemented (`scripts/build-image.sh`,
> `provision-appliance.sh`, `dataprep.sh`, `lockdown.sh`). What remains is
> booting a freshly-flashed card end-to-end (task tracked separately) — the
> overlay + partition changes are reboot-gated and card-coupled, so they're
> exercised by an actual flash, not retrofitted on the live dev card.

The overlay makes *everything* under `/` volatile, so `/data` must be its own
partition and the box must personalize `/etc` (hostname, the AP profile) while
still writable. That dictates the sequence: the image ships with the overlay
**off**, and the **first boot** runs a chain that ends by locking down:

    dataprep  →  firstboot  →  (backend + kiosk)  →  lockdown  →  reboot
    ─────────     ─────────      ────────────────      ────────
    make /data    per-device     box comes up once     persist NM conns,
    partition     secrets, AP,   on the writable       enable overlay,
    from free     hostname       root                  reboot read-only
    card space

- **App is RO-root-ready** (`docs/architecture.md` "Data model & read-only
  root"): the backend writes only under `/data` (atomic rename in-dir); the
  renderer writes only `/tmp/livepi` (tmpfs); the renderer reads shows/clips
  from `LIVEPI_DATA_DIR` via `livepi::userDataPath()` while shaders/config stay
  on the app tree; the kiosk unit points `XAUTHORITY` at a tmpfs
  `RuntimeDirectory` (X's cookie can't be written to the RO home otherwise).
- **Logs to RAM** — `config/journald-volatile.conf` (`Storage=volatile`) so the
  journal never touches the card.
- **`/data` partition** (`scripts/build-image.sh`, `scripts/dataprep.sh`,
  `livepi-dataprep.service`) — the build plants a small ext4 `LIVEPI_DATA`
  partition as the **last** partition, right after rootfs, so rootfs is boxed in
  and can't auto-grow. The first-boot oneshot then **grows** that partition to
  fill the card (`growpart` + `resize2fs`, idempotent), mounts `/data`, and adds
  the `nofail` `LABEL=LIVEPI_DATA` fstab entry. Belt-and-braces, the build also
  masks the stock rootfs-auto-grow services (`resize2fs_once`, `rpi-resize` →
  `systemd-growfs-root`) and strips any `init=` first-run hook. Runs before
  firstboot, which then writes the per-device secrets into `/data`. (A fallback
  path still *appends* a partition for images built before the trailing-seed
  change.)
- **Overlay** (`scripts/lockdown.sh`, `livepi-lockdown.service`) — the last
  first-boot step, after firstboot has personalized `/etc`. It enables the
  overlay (`raspi-config nonint enable_overlayfs`: real root mounted read-only,
  tmpfs on top) and reboots once. **Crucially it then patches the cmdline to
  `overlayroot=tmpfs:recurse=0`** — the `overlayroot` package's default
  (`recurse=1`) overlays *every* mount, which would wrap `/data` in a volatile
  tmpfs too (all user data lost on reboot); `recurse=0` confines the overlay to
  `/` alone, leaving `/data` a real persistent partition. Strictly fail-safe —
  it never reboots unless every step succeeded, and `LIVEPI_LOCKDOWN=0` builds a
  writable dev image. Guarded by `ConditionKernelCommandLine=!boot=overlay`, so
  it's a no-op once locked.
- **NM connections persist on `/data`** — lockdown bind-mounts
  `/etc/NetworkManager/system-connections` onto `/data/network/…` before
  enabling the overlay, so venue WiFi a customer joins later (and the control
  AP) survive the now-volatile `/etc`.
- **Boot stays RW in v1** — `/boot/firmware` is left writable (firmware/config
  edits are rare and low-risk). The rootfs overlay is what guards against an
  unclean-shutdown corrupting the OS or the app; write-protecting `/boot` is a
  later hardening step.

## First boot & device identity

> **Status: built.** `scripts/firstboot.sh` +
> `systemd/livepi-firstboot.service.template` implement the per-device
> secrets, `/data` seeding, unique hostname, and mDNS below; the backend unit
> loads the generated `/data/backend/.env`. See `docs/deploy.md`
> "First-boot provisioning". Deferred from this slice: the shared
> `livepi.local` alias (-> networking phase) and `/data` filesystem-expand
> (-> read-only-root/image work).

A first-boot service (systemd oneshot, guarded by a marker file on `/data`)
personalizes the box:

- **Unique hostname** `livepi-XXXX` derived from the Pi serial/MAC, so multiple
  boxes on one network don't collide.
- **Per-device secrets** -- generate a random `LIVEPI_PASSWORD` and
  `LIVEPI_SECRET_KEY` into `backend/.env` on `/data` (loaded by the backend
  unit's `EnvironmentFile`). This closes the shared-default-secret gap.
  Print/display the initial password (see on-screen help) and require a change
  on first login via the existing `POST /api/auth/password` flow.
- **mDNS** via avahi so the box is reachable at **`http://livepi.local`** with
  no IP hunting.
- **Seed a show** -- the backend already auto-creates `shows/default.json` on an
  empty `/data`, so a fresh box boots to something.

## Networking & provisioning

The hard part, and the differentiator. Managed with **NetworkManager** (the
Trixie default), which handles both hotspot and client profiles and switches
between them.

> **Status: built** (rolled our own on NM directly, over comitup/wifi-connect
> which have documented Trixie/NM friction). Verified on the Pi 4:
> - Web UI **Network** page + `/api/network/{status,scan,connect,forget}`
>   (`backend/livepi_backend/network.py`) drive NM via `nmcli`; a polkit rule
>   (`scripts/install-network-perms.sh`) lets the headless backend do so.
> - **Standing control AP** `LivePi-XXXX` created by firstboot; the
>   **autohotspot** mode-switch is native NM autoconnect priority (AP at -999,
>   venue clients outrank it) -- no dispatcher.
> - **Captive portal**: joining the AP auto-opens the UI, via probe responders
>   (`captive.py`) + selective DNS hijack of OS probe domains only
>   (`dnsmasq-shared.d/livepi-captive.conf`, so clients keep real internet) +
>   an nft `:80->:8080` redirect (`livepi-captive.nft`).
> - The brcmfmac radio **scans for venue networks while the AP is up**, so the
>   scan list is live during provisioning.
>
> Remaining: WiFi **country/regdomain** must be set for the radio to broadcast
> (imager/image build); the shared `livepi.local` alias; USB-dongle dual-radio.
> See `docs/deploy.md` "WiFi provisioning".

**The box's own access point is not just for setup -- it is a standing control
network.** A touring musician frequently has no usable venue WiFi, so the AP has
to be a first-class way to run the whole show, not a throwaway provisioning step.

- **Ad-hoc control AP (the default).** With no configured venue network, the box
  brings up its own hotspot `LivePi-XXXX`. A phone or laptop joins it and reaches
  the **full web UI -- edit *and* Live mode -- at `http://livepi.local`** (or the
  AP gateway IP). This means **a complete performance is controllable with zero
  venue WiFi and zero internet.** For many gigs this is the primary mode, not a
  fallback.
- **Captive portal for provisioning.** When there *is* a venue network worth
  joining, the same AP serves a captive portal (evaluate **`wifi-connect`** vs
  **`comitup`** -- both NetworkManager-based) where the phone picks the SSID and
  enters the password, switching the box to client mode. Web-based and
  **universal -- iOS and Android, no app install** -- which is why it's the
  primary provisioning path over Bluetooth.
- **Single-radio reality.** The Pi's one WiFi radio is an access point *or* a
  client at a time, not both. So the default is a **mode switch** (an
  "autohotspot" pattern: fall back to AP whenever no known network is in range).
  For the person who genuinely wants "on the venue WiFi *and* still offering my
  own control AP" simultaneously, a **USB WiFi dongle** (second radio) is the
  documented option.
- **On-screen help -- LivePi has a display, most headless gear doesn't.** When
  the box is unprovisioned or has no connected clients, the renderer paints the
  connection info and a **QR code** onto the video output (the projector the
  musician already plugged in). Scan it, land on the setup page or join the AP.
- **Fallbacks & re-provisioning.** A **USB keyboard** drives an on-screen
  network picker for anyone who prefers it. Changing networks at a new venue is
  reachable from the web UI ("forget network" -> drops back to AP) or by
  **holding the Pisound button at boot** to force AP mode.

Bluetooth provisioning (the phone sending WiFi over BLE, via the open **Improv
Wi-Fi** standard) is the slickest phone flow and is planned for v2 -- but it's
Android-Chrome-first (iOS Safari has no Web Bluetooth, so iPhone users would need
a companion app), which is why the universal captive portal leads. See
`docs/videosynth-backend.md`'s existing "venue networks" note, which flagged
this whole area as future work.

## Updates

Reuse the app-vs-data boundary: updates replace the *app*, never touch `/data`.

- **v1 -- in-app update.** An "Update available" banner in the web UI (the same
  pattern as the existing `JobsBanner`) pulls a **signed release bundle** from
  GitHub Releases -- a prebuilt `aarch64` binary + backend + `frontend/dist`,
  with a checksum/signature the box verifies before applying. It swaps the app,
  leaves `/data` alone, keeps the previous version, and **auto-rolls-back** if
  the new one fails a health check on restart. On a read-only root this means the
  updater remounts `/` read-write only for the swap (or the app lives on a small
  dedicated writable `/app` partition); base OS/apt changes are rare and stay an
  occasional re-image.
- **Later -- A/B root.** For truly un-brickable over-the-air updates, an A/B
  root-partition scheme with **RAUC** or **SWUpdate** (both FOSS -- aligned with
  this project): write the new rootfs to the inactive partition, reboot into it,
  auto-roll-back if it fails to come up. Overkill early, standard at scale, and
  `/data` stays untouched across the switch.

The app changes weekly; the OS rarely. The in-app updater is for the former, a
re-image or apt for the latter.

## Security posture

- **Per-device secrets** (above) replace the shared dev defaults -- the one
  genuine security must-fix before the first image ships.
- **LAN/AP-only, plain HTTP.** The web UI is served over plain `http` on the
  local network or the box's own AP, gated by the session password. That's
  typical for this class of gear; flagged here so it's a conscious choice, not an
  oversight. The password-change flow (`POST /api/auth/password`) already exists.
- **AP security** -- decide between WPA2 on the control AP (a printed/on-screen
  key) versus an open AP whose control is gated only by the web-UI password.

## Consumer touches

- **USB clip auto-import** -- plug in a USB stick of videos and the box ingests
  them automatically, extending the existing upload/transcode pipeline. The most
  musician-friendly way to load footage, no laptop required.
- **On-screen connection card** -- the boot/idle screen shows `livepi.local`,
  the AP name, and the QR (see networking).
- **`livepi.local`** everywhere so nobody types an IP.

## Build pipeline

> **Status: built (`scripts/build-image.sh`).** Produces
> `livepi-<version>.img.xz` from stock Pi OS Lite Trixie. Not yet run
> end-to-end / flashed (the first real build is the validation).

**Self-contained builder, not CustomPiOS.** The earlier plan named CustomPiOS
(the OctoPi framework). In practice the hard logic — the kiosk bake, the units,
and the read-only-root / partition orchestration — is identical regardless of
framework and now lives in **framework-agnostic scripts**, so wrapping it in a
transparent builder we fully control (and can read line-by-line) beat taking on
CustomPiOS's version drift + heavier surface. This also fits the project's
ethos (it already vendors openFrameworks and owns its whole toolchain). A
CustomPiOS wrapper could still sit on top later, since the logic is reusable.

- **`scripts/provision-appliance.sh`** — the shared "turn this root filesystem
  into a LivePi appliance" step: app user, `setup-pi.sh` (oF + deps + Pisound)
  + `make`, kiosk bake, render + enable every unit, WiFi regdomain, disable the
  stock rootfs auto-grow, golden-master hygiene. Path-agnostic (chroot or a real
  Pi); only ever `enable`s units so it's chroot-safe.
- **`scripts/build-image.sh`** — the host wrapper (**emulated build**, chosen
  over build-on-a-real-Pi-then-capture): downloads the base image, loop-mounts
  it, runs the provisioner in a **qemu-aarch64 `binfmt` chroot**, repacks to
  `.img.xz` + sha256. Runs on any x86_64/arm64 Linux host (`--check` does
  preflight only). Handles the chroot gotchas (`ld.so.preload` libarmmem,
  `/boot/firmware`, a hard unmount/detach cleanup trap). The stock rootfs
  auto-grow is disabled so the flashed card's free space is left for
  `dataprep`'s `LIVEPI_DATA`.
- **The oF/app compile under emulation is the slow part** (~tens of minutes).
  `LIVEPI_PREBUILT=1` is a hook to inject a Pi-built binary instead — the same
  artifact the in-app updater will ship; wire it up fully once the updater
  exists.
- **CI** — `build-image.sh` is CI-ready (qemu/binfmt); add a runner + release
  upload when the cadence warrants.
- **Versioning** -- semver (`git describe` feeds the image name), with a stable
  and a beta release channel.

## Roadmap / phasing

- **v1** -- single Pi 4 + Pi 5 image (with the Pi 5 decode/GL bring-up TODOs
  above) + read-only root / `/data` + ad-hoc control AP & captive portal +
  per-device secrets + in-app update. This is a shippable appliance.
  Progress: **per-device secrets + first-boot personalization done**
  (`firstboot.sh`), **networking done** (control AP + autohotspot + web-UI
  provisioning + captive portal, all verified on the Pi 4), and **read-only
  root + the image pipeline built** (`build-image.sh` /
  `provision-appliance.sh` / `dataprep.sh` / `lockdown.sh`) — pending a
  first end-to-end flashed-card validation. The remaining v1 piece is the
  in-app updater.
- **v2** -- Improv BLE provisioning (Android-first), A/B-root OTA, USB clip
  auto-import, the pre-flashed hardware SKU, and the Pi 5 optimizations (native
  reverse, hardware HEVC decode).

## Open questions

- **Captive-portal tool** -- `wifi-connect` (balena, Rust, purpose-built) vs
  `comitup` (Python, Debian-friendly). Prototype both against a real Pisound Pi
  before committing.
- **Simultaneous AP + uplink** -- accept the single-radio mode-switch, or make a
  USB WiFi dongle a supported (even bundled) accessory for the always-on-control
  case?
- **One image vs per-model** -- can a single image runtime-adapt cleanly across
  Pi 4 and Pi 5 (decode path, GL), or is a per-model image less fragile? Leaning
  single-image; revisit if the Pi 5 decode branch gets hairy.
- **Pi 5 decoder selection** -- let GStreamer autoplug the software H.264
  decoder, or select it explicitly in `ClipPlayer` by model?
- **On-screen QR at setup** assumes a display is attached during first
  networking -- true for a video box being plugged into a projector, but confirm
  it's not a chicken-and-egg for a bench setup.
- **Update-bundle signing** -- key custody and the trust root for verifying
  release bundles on-device.
- **Always-on control AP even when on venue WiFi** -- worth the USB-dongle
  requirement, or is the mode-switch enough for real gigs?
- **Pisound revision support matrix** -- which revisions each image generation
  officially supports (v1.1 Pi 4, v1.2 Pi 5).
