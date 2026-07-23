#!/usr/bin/env bash
# One-command update deploy from the desktop. Builds the web UI, compiles the
# arm64 bundle in the persistent build chroot (scripts/build-chroot.sh), drops
# livepi-app-<version>.tar.zst under ./bundles/, and (with --apply) uploads +
# applies it to a live box through the same engine the web UI drives.
#
#   scripts/deploy-update.sh --version 1.4.0                          # full: renderer + backend + UI
#   scripts/deploy-update.sh --version 1.4.0+ui --frontend-only       # UI-only (no compile)
#   scripts/deploy-update.sh --version 1.4.0 --apply pi@livepi-5779.local
#
# Run as your normal user. The web build + ssh/scp run as you (so your SSH key
# is used); only the chroot compile is elevated, via a single sudo call, so keep
# passwordless sudo or expect one prompt. One-time setup first:
#   sudo scripts/build-chroot.sh bootstrap
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VERSION=""; OUT="$REPO_DIR/bundles"; FRONTEND_ONLY=0; APPLY=""; NO_BUILD=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)       VERSION="$2"; shift 2 ;;
        --out)           OUT="$2"; shift 2 ;;
        --frontend-only) FRONTEND_ONLY=1; shift ;;
        --apply)         APPLY="$2"; shift 2 ;;
        --no-build)      NO_BUILD=1; shift ;;   # reuse the existing frontend/dist
        -h|--help)       sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done
[[ -n "$VERSION" ]] || { echo "need --version <v> (must differ from the running version)" >&2; exit 2; }

log() { printf '\n\033[1;36m[deploy-update]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[deploy-update] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# 1. web UI (as this user -- root-owned dist would block your next npm run)
if [[ "$NO_BUILD" == 0 ]]; then
    log "building the web UI"
    ( cd "$REPO_DIR/frontend" && npm run build ) || die "frontend build failed"
fi
[[ -f "$REPO_DIR/frontend/dist/index.html" ]] || die "frontend/dist not built (drop --no-build, or run npm run build)"

# 2. arm64 bundle via the persistent chroot (the one privileged step)
mkdir -p "$OUT"
log "building the arm64 bundle in the chroot (sudo)"
chroot_args=(build --version "$VERSION" --out "$OUT")
[[ "$FRONTEND_ONLY" == 1 ]] && chroot_args+=(--frontend-only)
sudo -E "$REPO_DIR/scripts/build-chroot.sh" "${chroot_args[@]}" \
    || die "chroot build failed (bootstrapped? 'sudo scripts/build-chroot.sh bootstrap')"

BUNDLE="$OUT/livepi-app-$VERSION.tar.zst"
[[ -f "$BUNDLE" ]] || die "bundle not produced at $BUNDLE"
log "bundle ready: $BUNDLE"

# 3. optional: upload + apply to the live box (as this user, your SSH key)
if [[ -n "$APPLY" ]]; then
    log "uploading + applying to $APPLY"
    scp "$BUNDLE" "$APPLY:/tmp/livepi-update.tar.zst" || die "scp failed -- reachable? key set up?"
    # Drop into the app user's incoming and drive the SAME apply the web UI uses
    # (verify -> swap -> restart -> health-gate -> auto-rollback). app-activate
    # is called by its factory path so the passwordless sudoers rule matches.
    ssh "$APPLY" 'set -e
        cp /tmp/livepi-update.tar.zst /data/app/incoming/pending.tar.zst
        rm -f /data/app/incoming/pending.sha256
        sudo /opt/livepi/scripts/app-activate.sh apply' \
        || die "apply failed on $APPLY"
    log "apply started on $APPLY -- watch Settings > Software update, or: ssh $APPLY journalctl -t app-activate -f"
    log "(a version that doesn't come up healthy auto-rolls-back -- you can't brick it)"
else
    log "not applied. Upload $BUNDLE in Settings > Software update, or re-run with --apply <user@box>."
fi
