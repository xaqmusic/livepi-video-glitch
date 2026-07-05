# LivePi VideoGlitcher

A real-time video-glitch box for live performance: a Raspberry Pi 4/5 +
[Pisound](https://blokas.io/pisound/) plays back CRT-filmed clips and mangles
them in GLSL shaders (horizontal sync tears, chromatic aberration,
stutter/freeze), synced to an external MIDI clock and modulated by MIDI CC
and live audio input.

See `docs/LivePi VideoGlitcher HLD.pdf` for the original concept and
`docs/architecture.md` for the current design, including a few corrections to
that original doc's hardware assumptions.

## Status

Early scaffold -- see `docs/architecture.md` for the phased roadmap. Nothing
has been built/run yet; Phase 0 (installing openFrameworks and confirming a
first compile) is the next hands-on step.

## Quickstart (desktop)

```sh
./scripts/setup-desktop.sh   # installs openFrameworks + ofxMidi as a sibling dir
make
./scripts/run-mock.sh
```

With no hardware attached, the app runs against `MockControlSource`:

| Key       | Action                           |
|-----------|-----------------------------------|
| `space`   | scene-change button               |
| `[` / `]` | knobA down / up (bidirectional)   |
| `,` / `.` | knobB down / up (intensity)       |
| `-` / `=` | tempo down / up                   |
| `d`       | toggle debug overlay              |

## First-time Pi network setup

Once the Pi is flashed with Raspberry Pi OS Lite 64-bit Trixie (see
`docs/deploy.md` for why) and on the network. Every command below runs on
your **desktop** unless a step says otherwise -- your desktop is always the
one *initiating* the `ssh`/`rsync`/`ssh-copy-id` connection, never the Pi.

1. **Enable SSH -- run this ON THE PI** (via a keyboard+monitor plugged
   into it, since SSH isn't reachable yet at this point). Skip this step
   entirely if you already enabled SSH in Raspberry Pi Imager's advanced
   settings (gear icon / Ctrl+Shift+X) before flashing:
   ```sh
   sudo raspi-config   # 3 Interface Options -> SSH -> Enable
   # or, without the menu:
   sudo systemctl enable --now ssh
   ```
2. **Find its IP** -- either `hostname -I` **on the Pi** itself, or check
   your router's DHCP client list from your desktop.
3. **If you've SSH'd into this Pi before under a previous OS install**
   (from here on, everything is run **on your desktop**), your desktop has
   the *old* host key cached and will refuse to connect with a "man in the
   middle" warning -- this is expected after a reflash, not an actual
   attack. Clear the stale entry, then reconnect:
   ```sh
   ssh-keygen -R <pi-ip-or-hostname>
   ssh <username>@<pi-ip-or-hostname>   # accept the new host key fingerprint
   ```
4. **Set up key-based auth** so `scripts/deploy-to-pi.sh` doesn't prompt for
   a password on every sync. Both commands run on your desktop --
   `ssh-keygen` generates a keypair locally, and `ssh-copy-id` pushes the
   *public* half of it to the Pi over a normal password-authenticated SSH
   connection:
   ```sh
   ssh-keygen -t ed25519 -C "livepi-deploy"   # skip if you already have a key you like
   ssh-copy-id <username>@<pi-ip-or-hostname>
   ```
5. **Configure the deploy pipeline** -- on your desktop, copy `.env.example`
   to `.env` and fill in `PI_HOST`, `PI_USER` (the username you set in
   Imager, not necessarily `pi`), `PI_APP_DIR`, `PI_OF_ROOT`. This file is
   only ever read by scripts running on your desktop (`deploy-to-pi.sh`);
   it never needs to exist on the Pi itself.

## Deploying to the Pi

See `docs/deploy.md`.
