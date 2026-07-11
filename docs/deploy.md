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
