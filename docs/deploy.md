# Deploying to the Pi

This is the **developer/rehearsal** deploy -- rsync from a desktop to a Pi you
already set up by hand. For the **consumer distribution** plan (a flashable
golden image, phone-only WiFi provisioning, read-only root, in-app updates,
Pi 4 + Pi 5), see `distribution.md`.

## OS choice

**Raspberry Pi OS Lite, 64-bit, Trixie.** Not Patchbox OS (what this
project's Pi previously ran), for two reasons verified against Blokas' own
Patchbox docs:

- Patchbox OS's supported-hardware list stops at the Pi 4B -- **no listed Pi
  5 support at all.**
- Even on a Pi 4, Patchbox bundles a real-time audio kernel, a lightweight
  desktop environment, and a "patch"-switching system built around swapping
  between pre-made audio tools (Pure Data, guitar-amp sims, etc.). None of
  that serves a single dedicated video-glitch appliance, and the bundled
  desktop actively works against the minimal X11-kiosk setup below.

Trixie over Bookworm: Raspberry Pi OS moved to Trixie (Debian 13) in
October 2025 with a newer 6.12 LTS kernel and better Pi 5 support. Pisound
packages weren't ported at first (installer 404'd), which would have made
Bookworm the safer pick -- but Blokas shipped Trixie support on
2025-11-26, so that gap is closed. Pisound's driver install isn't
Patchbox-exclusive either way -- `scripts/setup-pi.sh` runs Blokas' own
install script (`curl https://blokas.io/pisound/install.sh | sh`, adds
their apt repo), documented to work on plain Raspberry Pi OS directly.

Flash Raspberry Pi OS **Lite** (no desktop) so there's no auto-login desktop
session fighting the kiosk for the display, enable SSH in the imager, then:

```sh
git clone <this repo> ~/livepi-video-glitch    # or rsync it over, see below
cd ~/livepi-video-glitch
./scripts/setup-pi.sh
```

## Rehearsal-time deploy: desktop -> Pi

`scripts/deploy-to-pi.sh`, driven by `.env` (copy `.env.example`):

1. `ssh` reachability check -- fails loudly and fast if the Pi isn't up
   rather than hanging.
2. `rsync` source/shaders/config to `$PI_APP_DIR` (`--filter='merge
   .rsyncfilter'` excludes full-resolution `bin/data/clips/*` but keeps
   `bin/data/clips/samples/` -- full footage is synced separately/rarely,
   not on every code tweak).
3. `ssh ... make OF_ROOT=$PI_OF_ROOT -j$(nproc)` -- always a **native**
   build on the Pi's own architecture, never cross-compiled.
4. `--restart` flag additionally bounces the systemd service so a shader
   tweak goes live without touching the Pi physically:
   ```sh
   ./scripts/deploy-to-pi.sh --restart
   ```

This is intentionally rsync+ssh, not `git pull` on the Pi -- tweaking a
shader minutes before a set shouldn't require commit discipline, and the Pi
doesn't need its own git remote/auth configured at all.

## Button wiring

After `setup-pi.sh`:

```sh
sudo cp scripts/pisound/advance-scene-btn.sh /usr/local/pisound/scripts/pisound-btn/
sudo chmod +x /usr/local/pisound/scripts/pisound-btn/advance-scene-btn.sh
sudo pisound-config   # wire it to a click pattern (e.g. single click)
```

## Autostart at boot (kiosk)

GLFW (oF's Linux window backend) has no raw KMS/DRM path, so "boot straight
to a bare fullscreen GL surface" isn't possible with vanilla oF. The
well-trodden path is a minimal X11 session running only this binary:

Run this **on the Pi**, from inside the repo:

```sh
./scripts/install-systemd-unit.sh          # runs as the current user
# or: ./scripts/install-systemd-unit.sh someoneelse
sudo systemctl enable --now livepi-video-glitch
```

`install-systemd-unit.sh` renders `systemd/livepi-video-glitch.service.template`
with the actual account name and repo path baked in (`User=`/`Group=`/
`WorkingDirectory=`/`ExecStart=`) and installs it -- no manual editing of the
unit file, and safe to re-run if the repo moves or a different account
should run the kiosk. The unit runs `startx <binary> -- -s off -dpms` in the
`video`/`render`/`audio` groups needed for `/dev/dri` and ALSA access, with
`Restart=on-failure`.

**Verified against real hardware on the Pi 4** (with the fixes below already
applied): `sudo systemctl enable --now livepi-video-glitch` brought up X on
`:0` and the app rendered successfully with zero manual steps, stable and
still running a minute later at full CPU (not crash-looping). Confirmed:
VT ownership was never an issue (Raspberry Pi OS Lite's default `getty@tty1`
auto-login didn't conflict -- `startx` picked its own VT), group
permissions granted `/dev/dri` access (GL context initialized fine), no
default service grabbed a conflicting X session. One harmless line to
expect in the journal: `error: XDG_RUNTIME_DIR is invalid or not set in
the environment` -- logged once at startup, doesn't stop the app.

**Found while first bringing this up on a Pi 3** (see "GL / GLES
portability" in `architecture.md` for the fuller story -- X itself crashed
before getting this far, so the systemd unit specifically hasn't been
tested yet either):

- Raspberry Pi OS **Lite** doesn't ship any X11 packages at all --
  `sudo apt-get install xinit xserver-xorg xserver-xorg-legacy
  x11-xserver-utils` first.
- `/etc/X11/Xwrapper.config`'s default `allowed_users=console` refuses to
  start X unless launched from an active console/seat login session --
  needs `allowed_users=anybody`, since the systemd kiosk service won't
  have an interactive login present either. Edit the file directly (it's
  regenerated by `dpkg-reconfigure xserver-xorg-legacy` if ever reset).

## First-boot provisioning (golden-image boxes)

The rsync dev flow above keeps the shared `livepi` password and repo-local
`bin/data`. A **shipped** box instead personalizes itself on first boot --
this is the first slice of the `distribution.md` appliance work, and it's
usable/testable now (on a spare card) ahead of the full golden image.

`scripts/firstboot.sh` runs once, as root, before the backend and kiosk
start (`systemd/livepi-firstboot.service.template`, ordered `Before=` both).
Guarded by a marker (`$DATA_DIR/.provisioned`) so it's a no-op on every
later boot, it:

- **Generates per-device secrets** -- a random `LIVEPI_PASSWORD` (12 chars,
  unambiguous, grouped `xxxx-xxxx-xxxx`) and a 256-bit `LIVEPI_SECRET_KEY`
  into `$DATA_DIR/backend/.env`, replacing the shared `livepi` /
  `livepi-dev-secret-change-me` dev defaults. This is the security must-fix:
  every shipped box gets its own login and its own session-signing key.
- **Seeds `$DATA_DIR`** (shows/clips dirs) and hands it to the app user; the
  backend self-seeds a default show into it.
- **Sets a unique hostname** `livepi-XXXX` from the Pi's serial and enables
  avahi, so the box answers at **`http://livepi-XXXX.local`** with no IP
  hunting. (The shared `http://livepi.local` alias lands with the networking
  phase, where the AP gateway gives a stable target.)

The backend unit loads the generated secrets via a second `EnvironmentFile=`
pointing at `$DATA_DIR/backend/.env` (later-wins over the in-repo one; both
optional, so a dev Pi with no `/data` still starts). The secrets must exist
in the environment *before* the process starts -- `auth.py` binds
`SECRET_KEY` into a `TimestampSigner` at import time.

Install on an image / spare card (`/data` is the writable data partition):

```sh
./scripts/install-firstboot-unit.sh pi /data   # app user, data dir
sudo systemctl enable livepi-firstboot          # runs on next boot
# then install the backend unit pointed at the same data dir:
./scripts/install-backend-unit.sh pi /data
```

First login then prompts for a password change (the box shipped with a
printed one): the web UI reads `GET /api/auth/status` -> `mustChangePassword`
and pops the change-password dialog. It's a strong nudge, not a hard wall --
the printed password already works, so a confused user is never locked out.

Dry-run the secret/seed logic off-Pi (skips hostname/avahi/chown):

```sh
LIVEPI_DATA_DIR=/tmp/livepi-data LIVEPI_PROVISION_SYSTEM=0 ./scripts/firstboot.sh
```

## WiFi provisioning (control AP + venue onboarding)

The box runs its own **control-network AP** by default and lets a phone put it
onto venue WiFi from the web UI's **Network** tab -- no terminal. Built on
NetworkManager directly (see `distribution.md` for why not comitup/wifi-connect).
Requires an unblocked WiFi **country/regdomain** (set it in Raspberry Pi Imager,
or `sudo raspi-config` → Localisation → WLAN Country) or the radio won't
broadcast.

One-time install on the Pi (the golden image bakes these):

```sh
./scripts/install-network-perms.sh   # polkit: let the headless backend drive NM
./scripts/install-captive.sh          # captive-portal DNS hijack + :80->:8080 nft
```

Without the polkit rule every `nmcli` call from the backend returns "not
authorized" (a no-seat systemd service can't satisfy NM's interactive polkit
default). `firstboot.sh` creates the AP profile itself.

How it works:

- **Standing AP** `LivePi-XXXX` (`nmcli` profile `livepi-ap`, WPA2 key = the
  printed login password), `ipv4.method shared` for DHCP + NAT. Reach the UI at
  the AP gateway (`http://10.42.0.1:8080`).
- **Autohotspot** is native NM: the AP is `autoconnect=yes` at
  `autoconnect-priority -999`, so any saved venue network (default priority 0)
  outranks it -- NM keeps venue WiFi when in range and falls back to the AP
  when it isn't. No dispatcher script.
- **Provisioning**: the Network page scans (`nmcli device wifi list` -- works
  even while the AP is up on the Pi 4 radio), joins as a client (which switches
  the single radio off the AP), and can forget a network to drop back.
- **Captive portal**: `dnsmasq-shared.d/livepi-captive.conf` points the OS
  connectivity-check domains at the AP gateway (only those, so clients keep
  real internet), an nft rule redirects `:80`→`:8080`, and the backend's
  `captive.py` 302s the probe to the UI -- so joining the AP pops the phone's
  "Sign in to network" browser straight at LivePi.

Single radio: the AP and a venue-client link can't run at once (a USB WiFi
dongle is the documented path to both). Reaching the box over **Ethernet** is
the safe way to reconfigure WiFi without cutting your own connection.

## Building the golden image

The consumer path is a flashable image, not this deploy-and-build dance. One
command turns stock Raspberry Pi OS Lite (arm64, Trixie) into a
`livepi-<version>.img.xz` — it downloads the base image, runs the whole
provisioning inside a **qemu-aarch64 chroot**, and repacks. Runs on any Linux
host (x86_64 or arm64); no Pi needed to build.

```sh
# One-time host prerequisites (Debian/Ubuntu):
sudo apt-get install qemu-user-static binfmt-support parted \
                     xz-utils rsync e2fsprogs dosfstools nodejs npm

sudo ./scripts/build-image.sh --check     # verify the host is ready (no build)
sudo ./scripts/build-image.sh             # full build -> /var/tmp/livepi-image/
```

The heavy part is compiling openFrameworks + the renderer under emulation
(tens of minutes). Useful knobs (env vars): `LIVEPI_LOCKDOWN=0` builds a
**writable dev image** (skips the read-only-root flip — handy for poking at a
flashed card), `LIVEPI_WIFI_COUNTRY=GB` sets the regdomain,
`LIVEPI_OUTPUT_DIR=…` moves the artifact. See the header of
`scripts/build-image.sh` for the full list.

Under the hood `build-image.sh` calls `scripts/provision-appliance.sh` in the
chroot — the same script would provision a real Pi natively if you ever want to
capture a card instead. Flash the result with Raspberry Pi Imager or
balenaEtcher.

### What the first boot does

A freshly flashed card personalizes itself with no operator, in one chain
(each step is an idempotent, marker-guarded oneshot):

1. **`dataprep`** — appends a `LIVEPI_DATA` partition in the card's free space,
   formats it, mounts `/data`. (The image ships with the stock rootfs auto-grow
   disabled precisely so that free space is still there to claim.)
2. **`firstboot`** — generates the per-device password + secret key into
   `/data/backend/.env`, sets the unique `livepi-XXXX` hostname, and creates the
   control AP (WPA2 key = the printed password).
3. **backend + kiosk** come up on the still-writable root.
4. **`lockdown`** — persists the NetworkManager connections onto `/data`,
   enables the read-only overlay, and reboots once. From here the root is
   read-only; everything the customer changes lives on `/data`.

Boots after that are the sealed appliance: read-only root, writable `/data`,
and updates replace the app without ever reflashing (`docs/architecture.md`,
"Data model & read-only root").

> **Not yet validated end-to-end on hardware.** The builder and the first-boot
> chain are complete and statically checked, but a real flashed-card run (the
> overlay flip is reboot-gated) is still owed. `dataprep.sh` is dry-runnable
> off-Pi (`LIVEPI_DATAPREP_DRYRUN=1`) and `lockdown.sh` no-ops safely with
> `LIVEPI_LOCKDOWN=0` if you want to inspect the pieces first.
