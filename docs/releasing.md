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
sudo LIVEPI_VERSION=v1.0.0 scripts/build-image.sh      # full build (~1hr+ under qemu)
```
Output: `/var/tmp/livepi-image/livepi-v1.0.0.img.xz` + `.sha256`. The compile of
openFrameworks under emulation is the long, quiet part — not a hang.

**Knobs** (env; see the header of `build-image.sh` for all):
- `LIVEPI_VERSION` — version tag baked into `/opt/livepi/manifest.json` (the
  factory version the updater compares against). No git tags exist, so set this
  explicitly, e.g. `v1.0.0`.
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

## Cross-references
- `docs/distribution.md` — why the appliance is built this way (partitions,
  first boot, networking, update design, security posture).
- `docs/deploy.md` — provisioning a Pi by hand, button wiring, Wi-Fi onboarding.
- `scripts/app-activate.sh`, `build-image.sh`, `build-chroot.sh`,
  `deploy-update.sh`, `build-bundle.sh` — each script's header is the
  authoritative reference for its flags.
