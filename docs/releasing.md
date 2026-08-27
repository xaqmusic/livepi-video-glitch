# Releasing: building images and update bundles

There are **two** release artifacts, and choosing between them is the whole
mental model:

| Artifact | Script | Delivery | Changes | When |
|----------|--------|----------|---------|------|
| **Golden image** `livepi-<v>.img.xz` | `build-image.sh` | flash a card | *everything* (kernel, OS, units, app) | new box, or a rare from-scratch reflash |
| **Update bundle** `livepi-app-<v>.tar.zst` | `deploy-update.sh` | in-app upload | app only (renderer, backend, web UI, shaders, config, scripts) | every normal release — **no reflash** |

The app lives on the writable `/data` partition and is activated by an atomic
symlink (`/data/app/current`); the factory image at `/opt/livepi` is the
immutable rollback anchor. So an "update" swaps a symlink and restarts two
services — it never touches the read-only root. Kernel / apt packages / systemd
units / the overlay config are baked into the image and only change via a new
flash (a future A/B-root track — `docs/tech-debt.md` — will lift that).

**Tiers.** A **frontend-only** change (Tier 1) needs no compile. A change to the
renderer (C++) or backend (Tier 2) must be compiled for **arm64** — that's what
the persistent build chroot is for.

---

## Which box are you building for?

Two hardware routes, one image pipeline. The image **adapts at runtime** (see
`docs/distribution.md`), but the BUILD differs in one mandatory flag, and the
boxes differ in what you plug into them.

| | **Pi 4 + Pisound** | **Pi 5 + generic USB** |
|---|---|---|
| Build flag | *(default)* | **`LIVEPI_PISOUND=0` — mandatory** |
| Audio + MIDI | Pisound HAT | any USB MIDI / USB audio interface |
| Physical button | Pisound button (hold gestures) | PMIC power button (**tap counts**) |
| Power | 5V/3A | **5V/5A USB-C PD** |
| Cooling | heatsink | **Active Cooler** (this is why there is no HAT) |
| H.264 decode | hardware (`v4l2h264dec`) | software (`avdec_h264`), ~6x real-time |

> **Release gate, 2026-08: do NOT ship to a Pi 4 box yet.** Two changes are
> unverified on Pi 4 hardware and one affects every board — the KMS
> `OutputClass` and the kiosk unit moving X to **vt1**. There is currently no
> Pi 4 available to test against. Build for Pi 5 until that changes; see
> `docs/tech-debt.md`.

### Building the Pi 5 image

```sh
sudo LIVEPI_PISOUND=0 LIVEPI_VERSION=v1.1.0-pi5 scripts/build-image.sh
```

**`LIVEPI_PISOUND=0` is not optional.** The auto-probe cannot see a HAT from
inside the qemu chroot — there is no device tree there — so it deliberately
resolves to *install*, which is right for the Pi 4 image and wrong here.
Get it wrong and the image bakes in a `dtoverlay` and a DKMS module for an I2S
codec that isn't present, plus a `pisound-btn` service with no button.

Everything else about the Pi 5 is handled at runtime and needs no build flags:

- **Control input.** `control_source=auto` probes at boot: Pisound → any real
  MIDI/capture device → mock. A USB MIDI adapter and a USB audio interface are
  picked up with no configuration. If audio-reactive params sit at zero, check
  the renderer log for `audio input open:` — it reports what actually opened.
- **The power button replaces the Pisound button**, by TAP COUNT, because the
  Pi 5 force-cuts power on a hold at ~5s in hardware:

  | Gesture | Action |
  |---|---|
  | 1 tap | next scene |
  | 2 taps | debug overlay |
  | 3 taps | setup / QR card |
  | 5 taps | password reset (recovery) |

  Tap briskly. Holding does nothing on purpose, and past 1.5s the red LED
  blinks fast to warn that a force-off is coming.

### First things to do on a new Pi 5 box

1. **Log in** to `http://<hostname>.local:8080` with the printed device code and
   change the password when prompted.
2. **Turn off the network-install screen** — Settings ▸ Startup screen. That
   pink screen with a QR code at power-on is the *bootloader's*, which is why no
   kernel or config.txt setting suppresses it. Takes ~15 seconds to write and
   **applies at the next power-on**; do not cut power while it is writing. It is
   stored in the board's EEPROM, so it survives re-flashing the card — and it
   removes network-install as a recovery path for that board.
3. **Set the boot splash** — Settings ▸ Boot splash, from any still image you
   have uploaded. It covers the whole startup, so a venue sees a logo rather
   than a black screen if you ever restart mid-set.

### What a Pi 5 boot looks like

Roughly **60–75 seconds** from power-on to the first scene on a slow SD card,
and that is expected rather than a fault:

- ~12s — splash image appears (framebuffer), "LivePi is booting" spinner under it
- ~30–40s — X starts; **glamor init on V3D dominates this**, and the splash
  stays up through it
- then ~16s — the renderer pre-warms the video stack behind the splash, so the
  first clip scene of the set does not stall
- then the first scene

A faster SD card shortens most of this. See `docs/tech-debt.md` for the
measurements and what has already been ruled out.

---

## Building a golden image

Produces a sealed, read-only-root `.img.xz` you flash with Raspberry Pi Imager /
balenaEtcher. Built on an x86_64 (or arm64) Linux desktop via qemu-user
emulation — no Pi required.

**One-time host prerequisites** (Debian/Ubuntu):
```sh
sudo apt-get install qemu-user-static binfmt-support parted \
                     xz-utils rsync e2fsprogs dosfstools nodejs npm zstd
```

**Build:**
```sh
sudo scripts/build-image.sh --check                    # fast preflight (tools, binfmt)

# Pi 4 + Pisound:
sudo LIVEPI_VERSION=v1.0.0 scripts/build-image.sh      # full build (~1hr+ under qemu)

# Pi 5 + generic USB MIDI/audio (note the flag -- it is not optional):
sudo LIVEPI_PISOUND=0 LIVEPI_VERSION=v1.0.0-pi5 scripts/build-image.sh
```
Output: `/var/tmp/livepi-image/livepi-v1.0.0.img.xz` + `.sha256`. The compile of
openFrameworks under emulation is the long, quiet part — not a hang.

**Knobs** (env; see the header of `build-image.sh` for all):
- `LIVEPI_VERSION` — version tag baked into `/opt/livepi/manifest.json` (the
  factory version the updater compares against). No git tags exist, so set this
  explicitly, e.g. `v1.0.0`.
- `LIVEPI_PISOUND=0` — **mandatory for a Pi 5 image.** Skips Blokas' Pisound
  installer and the button map. The default (`auto`) cannot detect the absence of
  a HAT from inside the chroot and deliberately resolves to *install*; see
  "Which box are you building for?" above.
- `LIVEPI_LOCKDOWN=1` (default) ships a sealed read-only-root box; `=0` builds a
  writable **dev** image.
- `LIVEPI_DEV_SSH_KEY=/path/to/id.pub` bakes an SSH key into `~pi` for **test**
  boxes (works even on a sealed image, since it's in the read-only lower layer).
  Leave it unset for a real release.

**Build from a clean tree.** `build-image.sh` rsyncs your working tree. `/data`
seeds fresh on first boot (a Welcome show, empty library), so local dev *clips*
never ship — but to guarantee the image is exactly a known commit, build from a
throwaway git worktree:
```sh
git worktree add /tmp/livepi-rel <commit-or-tag>
cd /tmp/livepi-rel && sudo LIVEPI_VERSION=v1.0.0 scripts/build-image.sh
# when done:  git worktree remove /tmp/livepi-rel
```
This also matters when the *next thing* you ship is an update bundle that adds a
feature: build the image from the commit **before** that feature, so the update
genuinely adds it.

**First boot** does: `dataprep` (claim the card's free space as the `/data`
partition) → `firstboot` (per-device secrets + unique hostname + control AP) →
services → `lockdown` (enable read-only root) → one reboot. Boot 2+ is the sealed
appliance.

---

## Building an update bundle

> **Bundles are hardware-neutral; images are not.** A `.tar.zst` carries the
> app only, and the app adapts at runtime — so ONE bundle serves a Pi 4 and a
> Pi 5. `LIVEPI_PISOUND` is a build-time flag for the IMAGE and has no meaning
> here. But see the Pi 4 release gate above before sending anything to a Pi 4.

### One-time: bootstrap the build chroot
A persistent arm64 Raspberry Pi OS rootfs with the openFrameworks toolchain and
a repo checkout whose `obj/` survives between builds, so renderer changes
recompile **incrementally** (minutes, not a full image build).
```sh
sudo scripts/build-chroot.sh bootstrap     # long the first time (downloads Pi OS + builds oF)
```
Watch for `bootstrap DONE`. It's resumable — re-run to pick up after a failure.

### Each release
```sh
scripts/deploy-update.sh --version 1.2.0                 # full: compile + bundle
scripts/deploy-update.sh --version 1.2.0+ui --frontend-only   # Tier 1: no compile
scripts/deploy-update.sh --version 1.2.0 --apply pi@livepi-XXXX.local  # + push & apply
```
Run as your **normal user** — it builds the web UI and does scp/apply as you
(your SSH key), and self-elevates only the chroot compile via one `sudo` prompt.
Output: `bundles/livepi-app-1.2.0.tar.zst` + `.sha256`.

`--version` is an explicit label; it only has to **differ** from the version the
box is currently running (the updater rejects a same-version upload). Once the
chroot has compiled, you can also re-emit a bundle with **no sudo** straight off
the (world-readable) chroot tree:
```sh
scripts/build-bundle.sh --tree /var/tmp/livepi-build-arm64/rootfs/build/livepi \
                        --out ./bundles --version 1.2.0
```

### Applying a bundle
Two paths into the **same** engine (`scripts/app-activate.sh`):

- **Web UI** — Settings ▸ Software update ▸ upload `livepi-app-<v>.tar.zst` from
  any device on the box's network. This is the authentic path a sealed box
  (no SSH) uses.
- **`--apply pi@box`** — scp + trigger over SSH (needs the box reachable and the
  `/etc/sudoers.d/livepi-update` rule the image bakes in).

The engine: verify (sha256) → extract to `/data/app/versions/<v>` → atomic
`current` symlink swap → restart `livepi-backend` + `livepi-video-glitch` →
**health-gate** (backend `/api/health` + fresh renderer `status.json` within
~30s) → promote to `last-good`, or **auto-roll-back** to the previous version.
A boot-loop guard abandons a version that can't confirm across two boots. **You
cannot brick the box with a bundle.** Progress shows live in the UI; a
reboot-surviving log is at `/data/app/logs/apply.log`.

---

## Gotchas we've hit (so you don't)

- **Root steps need a real terminal.** `build-image`, `build-chroot`, and
  `deploy-update`'s compile all need `sudo`. A backgrounded `sudo` or one run
  through a non-interactive wrapper hangs on the password prompt (SIGTTIN); run
  them in a normal terminal.
- **Bundles must be world-readable.** The services run as `pi` and only read the
  app tree; `build-bundle.sh` `chmod`s the staged tree `a+rX` before tarring so a
  root-built (chroot) bundle still activates. (A bundle whose top dir is `700`
  fails the health-gate and rolls back — that's the symptom if you see it.)
- **journald is volatile on the appliance** (logs to RAM). After a reboot the
  systemd journal is gone — use `/data/app/logs/apply.log` for update history,
  and diagnose a *failed* bundle locally with `tar -tvf` on the `.tar.zst`
  (ownership/permissions and contents) before rebooting the box.
- **Host-key changed after a reflash.** Reusing an IP/hostname trips SSH's
  "REMOTE HOST IDENTIFICATION HAS CHANGED"; `ssh-keygen -R <host>` and reconnect.
- **A dev SSH key added at runtime doesn't survive** the read-only overlay — bake
  it at build time with `LIVEPI_DEV_SSH_KEY` instead.
- **Forgetting `LIVEPI_PISOUND=0` on a Pi 5 image** bakes in a `dtoverlay` and a
  DKMS module for a HAT that isn't there, plus a dead `pisound-btn` service. The
  auto-probe cannot save you: inside the qemu chroot there is no device tree, so
  it defaults to *install* on purpose (that default is what keeps the Pi 4 image
  correct). It is the one build flag that differs between the two routes.
- **The pink QR screen at power-on is not ours to suppress at build time.** It is
  the bootloader's network-install prompt, stored in the BOARD's EEPROM, so it
  survives flashing a fresh card and cannot be baked into an image. Turn it off
  per-board in Settings ▸ Startup screen after first boot.
- **On the Pi 5, mixing `make` and `make OF_ROOT=/abs/path`** flips openFrameworks'
  cached compiler-flag string and forces a full core-lib rebuild — tens of
  minutes, and if it is interrupted you get truncated object files and confusing
  link errors. Pick one form and stick to it; `deploy-to-pi.sh` uses the
  `OF_ROOT=` form.

## Cross-references
- `docs/distribution.md` — why the appliance is built this way (partitions,
  first boot, networking, update design, security posture).
- `docs/deploy.md` — provisioning a Pi by hand, button wiring, Wi-Fi onboarding.
- `docs/tech-debt.md` — the Pi 4 release gate, boot-time measurements, and the
  approaches already ruled out (worth reading before "fixing" boot time).
- `scripts/app-activate.sh`, `build-image.sh`, `build-chroot.sh`,
  `deploy-update.sh`, `build-bundle.sh` — each script's header is the
  authoritative reference for its flags.
