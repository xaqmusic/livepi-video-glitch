# CLAUDE.md

LivePi VideoGlitcher — a Raspberry Pi **videosynth appliance**: an
openFrameworks/C++ renderer + FastAPI backend + React/TS web UI, shipped as a
sealed read-only-root image with an in-app updater. Full docs in `docs/`; this
file is the fast path for picking work back up — especially **shipping update
bundles without hiccups**.

## Layout
- `src/` — oF renderer: `render/ fx/ scenes/ control/ video/ util/`
- `backend/livepi_backend/` — FastAPI: shows, clips, network, update, settings
- `frontend/` — React/TS (Vite)
- `scripts/` — setup, provisioning, image + bundle build, updater (`app-activate.sh`)
- `docs/releasing.md` (build runbook) · `docs/tech-debt.md` · `docs/architecture.md`

## Two release artifacts (details: docs/releasing.md)
- **Golden image** `build-image.sh` — flashable, whole-OS. New box / rare reflash.
- **Update bundle** `deploy-update.sh` — app-only `.tar.zst`, in-app upload,
  health-gated + auto-rollback, **no reflash**. The normal path.

## Shipping an update bundle
1. One-time: `sudo scripts/build-chroot.sh bootstrap` (persistent arm64 oF chroot; long).
2. `scripts/deploy-update.sh --version <v>` → `bundles/livepi-app-<v>.tar.zst`.
   - `--frontend-only` = UI-only (Tier 1, no compile); else it compiles the arm64 renderer (Tier 2).
   - `--apply pi@<box>` pushes+applies over SSH; otherwise upload in Settings ▸ Software update.
3. `<v>` only has to **differ** from the running version (no git tags; explicit label).
4. Engine: verify → swap `/data/app/current` → restart → health-gate (~30s) → promote or auto-rollback. Can't brick.

## Gotchas that cause hiccups (all learned the hard way)
- **Root steps need a REAL terminal.** `build-image` / `build-chroot` /
  `deploy-update`'s compile need `sudo` with a password; a backgrounded or
  non-interactive sudo hangs (SIGTTIN). Have the user run them in a terminal,
  `tee` to a log, and monitor the log.
- **Bundles must be world-readable.** `build-bundle.sh` chmods the staged tree
  `a+rX` so the `pi`-run services can read a root-built bundle; a `700` top dir →
  health-gate fails → rollback.
- **journald is volatile** on the box. Update history survives at
  `/data/app/logs/apply.log`; diagnose a failed bundle locally with `tar -tvf`
  **before** rebooting the box.
- **Build the image from the commit BEFORE a feature** if a bundle is meant to
  *add* that feature (throwaway `git worktree`), or the update test proves nothing.
- **Host key changes after a reflash** → `ssh-keygen -R <host>`.
- Adding a per-layer effect/transform param is **one line** in
  `backend/effects_manifest.json` (manifest-driven UI + validation + mapping);
  the renderer reads it generically. Scene-level fields (transition, renderScale,
  autoAdvance) round-trip via ShowLoader + the backend `Scene` model + the TS type.

## Conventions
- Commit only when asked; if on `main`, branch first. Don't push / flash /
  apply-to-hardware without sign-off. Commit trailers per the harness.
- Verify a renderer change compiles on the **desktop** (`make -j`) and the
  frontend (`npm run build`) before building an arm64 bundle.
- Sealed release images have no SSH; the dev/test box has been
  `livepi-5779.local` (dev image, my key baked in).
