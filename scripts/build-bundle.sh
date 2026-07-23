#!/usr/bin/env bash
# Build a LivePi update bundle -- the self-contained app tree the in-app updater
# drops on /data and makes live by symlink (scripts/app-activate.sh). Emits
#   livepi-app-<version>.tar.zst   + a matching .sha256
# whose contents extract to exactly what /data/app/versions/<version>/ must hold
# for the renderer + backend units (which resolve /data/app/current) to run.
#
# ARCH MATTERS: the renderer binary and backend/pylib (pydantic_core etc.) are
# aarch64, so a shippable bundle must be built FROM an aarch64 tree -- in
# practice ON a Pi, from the running app:
#
#   sudo scripts/build-bundle.sh --version 1.2.3            # bundles /data/app/current
#   # -> ./livepi-app-1.2.3.tar.zst  (upload it in the web UI's Software update)
#
# FRONTEND-ONLY fast path: for a UI-only change (no renderer/backend rebuild) the
# aarch64 binary + pylib are unchanged, so take them from the running app and
# just swap in a freshly built dist -- no full repo rsync to the Pi:
#
#   # on the desktop: npm run build, then rsync just the dist over, e.g.
#   #   rsync -a frontend/dist/ pi@livepi.local:/tmp/newdist/
#   sudo scripts/build-bundle.sh --frontend-only /tmp/newdist --version 1.2.3+ui
#
# Building from the x86 dev repo produces an x86 bundle -- fine for exercising
# the pipeline on a desktop, useless on the Pi; the script warns when the binary
# is not aarch64.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TREE=""; VERSION=""; OUT="$PWD"; DIST_OVERRIDE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --tree)          TREE="$2"; shift 2 ;;
        --version)       VERSION="$2"; shift 2 ;;
        --out)           OUT="$2"; shift 2 ;;
        # UI-only rebuild: everything from --tree, but bundle the dist at this
        # path instead of the tree's own. Value is a dir holding index.html (a
        # built dist), or a parent that contains dist/.
        --frontend-only) DIST_OVERRIDE="$2"; shift 2 ;;
        -h|--help)       sed -n '2,27p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

# Resolve the dist-override to the dir that actually holds index.html.
if [[ -n "$DIST_OVERRIDE" ]]; then
    if [[ -f "$DIST_OVERRIDE/index.html" ]]; then :
    elif [[ -f "$DIST_OVERRIDE/dist/index.html" ]]; then DIST_OVERRIDE="$DIST_OVERRIDE/dist"
    else echo "--frontend-only $DIST_OVERRIDE: no built dist there (expected index.html)" >&2; exit 1; fi
    DIST_OVERRIDE="$(cd "$DIST_OVERRIDE" && pwd)"
fi

# Default source tree: the running app on a Pi, else the factory tree, else the
# dev repo. All three are the same shape; only the arch differs.
if [[ -z "$TREE" ]]; then
    for cand in /data/app/current /opt/livepi "$REPO_DIR"; do
        [[ -x "$cand/bin/livepi-video-glitch" ]] && { TREE="$cand"; break; }
    done
fi
TREE="$(cd "$TREE" && pwd)"
BIN="$TREE/bin/livepi-video-glitch"
[[ -x "$BIN" ]] || { echo "no renderer binary at $BIN -- build it first (make)" >&2; exit 1; }

log() { printf '[build-bundle] %s\n' "$*"; }
warn() { printf '[build-bundle] WARNING: %s\n' "$*" >&2; }

# Warn (don't fail) if the renderer isn't aarch64 -- a desktop test bundle is a
# legitimate use, but a real Pi update must be arm64.
arch="$(LC_ALL=C file -b "$BIN" 2>/dev/null || true)"
case "$arch" in
    *aarch64*|*ARM\ aarch64*) : ;;
    "") warn "could not determine binary arch ('file' missing?); assuming ok" ;;
    *) warn "renderer binary is NOT aarch64 ($arch) -- this bundle will not run on a Pi" ;;
esac

# Version: explicit --version wins; else git describe when the tree is a repo;
# else carry the tree's current version with a timestamp so it's unique + clearly
# a rebuild (the UI needs a value distinct from the running one to show a change).
tree_field() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2],"") or "")' "$TREE/manifest.json" "$1" 2>/dev/null || true; }
if [[ -z "$VERSION" ]]; then
    if VERSION="$(git -C "$TREE" describe --tags --always --dirty 2>/dev/null)"; then :; else
        base="$(tree_field version)"; base="${base:-app}"
        VERSION="${base}+$(date -u +%Y%m%d%H%M%S)"
    fi
fi
GIT_HASH="$(git -C "$TREE" rev-parse --short HEAD 2>/dev/null || tree_field gitHash)"
GIT_HASH="${GIT_HASH:-unknown}"

log "tree=$TREE  version=$VERSION  git=$GIT_HASH"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/bin/data" "$STAGE/backend" "$STAGE/frontend"

# --- the runtime tree, exactly what the units resolve under /data/app/current --
# Renderer: the binary + its bundled shared libs (oF/fmod copied into bin/) +
# shaders + config (config carries the provision-generated app.local.json =
# fullscreen + auto control source; a bundle without it would render windowed).
cp -a "$BIN" "$STAGE/bin/"
shopt -s nullglob
for so in "$TREE"/bin/*.so; do cp -a "$so" "$STAGE/bin/"; done
shopt -u nullglob
cp -a "$TREE/bin/data/config"  "$STAGE/bin/data/"
cp -a "$TREE/bin/data/shaders" "$STAGE/bin/data/"

# The appliance render config (fullscreen + auto control source + /data paths)
# is written by provision-appliance.sh, so a tree built OUTSIDE provisioning --
# the qemu build chroot -- has no app.local.json, and the updated app would come
# up WINDOWED with the mock control source. Synthesize the same file provision
# writes when the tree didn't carry one; a Pi-built bundle (--tree
# /data/app/current) already has the real one and keeps it.
if [[ ! -f "$STAGE/bin/data/config/app.local.json" ]]; then
    warn "tree has no app.local.json -- writing the appliance default (fullscreen/auto)"
    cat > "$STAGE/bin/data/config/app.local.json" <<'APPCFG'
{
    "control_source": "auto",
    "window": { "fullscreen": true },
    "ui": { "claimed_marker": "/data/.claimed" },
    "controls": { "scene_map": "/data/settings.json" }
}
APPCFG
fi

# Backend: source + the RELOCATABLE aarch64 pylib. Drop per-box/dev state (the
# real secrets live on /data/backend/.env; pylib/pycache are rebuilt/derived).
rsync -a --exclude='.env' --exclude='.venv/' --exclude='__pycache__/' \
        --exclude='*.pyc' "$TREE/backend/" "$STAGE/backend/"
[[ -f "$STAGE/backend/pylib/uvicorn/__init__.py" ]] || warn "backend/pylib missing in the tree -- the backend will not start after update"

# Web UI (served from frontend/dist, resolved relative to the backend module, so
# it must ride inside the version tree) + scripts (so the tree is self-contained;
# the ACTIVE activator is always the factory copy, this is just completeness).
# --frontend-only swaps in a freshly built dist here; everything else above is
# still the running tree's (unchanged aarch64 binary/backend), so the bundle is
# a complete, valid app tree that differs from the current one only in the UI.
DIST_SRC="${DIST_OVERRIDE:-$TREE/frontend/dist}"
[[ -f "$DIST_SRC/index.html" ]] || { echo "frontend/dist missing at $DIST_SRC (npm run build)" >&2; exit 1; }
[[ -n "$DIST_OVERRIDE" ]] && log "frontend-only: dist from $DIST_SRC, everything else from $TREE"
cp -a "$DIST_SRC" "$STAGE/frontend/dist"
cp -a "$TREE/scripts" "$STAGE/"

# --- manifest at the tree root (app-activate.sh peeks ./manifest.json) ---------
cat > "$STAGE/manifest.json" <<MANIFEST
{
  "version": "$VERSION",
  "gitHash": "$GIT_HASH",
  "builtAt": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "channel": "bundle"
}
MANIFEST

# --- pack + checksum ----------------------------------------------------------
mkdir -p "$OUT"
bundle="$OUT/livepi-app-$VERSION.tar.zst"
log "packing $bundle"
# -19 -T0: strong, all cores. Members are ./manifest.json, ./bin/..., ./backend/...
tar -C "$STAGE" --use-compress-program='zstd -19 -T0' -cf "$bundle" .
( cd "$OUT" && sha256sum "$(basename "$bundle")" > "$(basename "$bundle").sha256" )

size="$(du -h "$bundle" | cut -f1)"
log "done: $bundle ($size)"
log "sha256: $(cut -d' ' -f1 "$bundle.sha256")"
