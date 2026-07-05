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
`docs/deploy.md` for why) and on the network:

1. **Enable SSH** (skip if you already enabled it in Raspberry Pi Imager's
   advanced settings, gear icon / Ctrl+Shift+X, before flashing):
   ```sh
   sudo raspi-config   # 3 Interface Options -> SSH -> Enable
   # or, without the menu:
   sudo systemctl enable --now ssh
   ```
2. **Find its IP** -- either `hostname -I` on the Pi itself, or check your
   router's DHCP client list.
3. **If you've SSH'd into this Pi before under a previous OS install**, your
   desktop has the *old* host key cached and will refuse to connect with a
   "man in the middle" warning -- this is expected after a reflash, not an
   actual attack. Clear the stale entry, then reconnect:
   ```sh
   ssh-keygen -R <pi-ip-or-hostname>
   ssh <username>@<pi-ip-or-hostname>   # accept the new host key fingerprint
   ```
4. **Set up key-based auth** so `scripts/deploy-to-pi.sh` doesn't prompt for
   a password on every sync:
   ```sh
   ssh-keygen -t ed25519 -C "livepi-deploy"   # skip if you already have a key you like
   ssh-copy-id <username>@<pi-ip-or-hostname>
   ```
5. **Configure the deploy pipeline** -- copy `.env.example` to `.env` and
   fill in `PI_HOST`, `PI_USER` (the username you set in Imager, not
   necessarily `pi`), `PI_APP_DIR`, `PI_OF_ROOT`.

## Deploying to the Pi

See `docs/deploy.md`.
