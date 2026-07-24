#!/usr/bin/env bash
# Persistent aarch64 build chroot for LivePi UPDATE BUNDLES. The renderer and
# the backend pylib are arm64, so a bundle that changes them must be COMPILED
# for arm64. Rather than a full image build (build-image.sh) every time, this
# keeps a Raspberry Pi OS arm64 rootfs on the desktop with the openFrameworks
# toolchain and a repo checkout whose obj/ SURVIVES between builds -- so after
# the one-time bootstrap, a renderer tweak recompiles incrementally (minutes)
# and emits a livepi-app-<version>.tar.zst you ship through the in-app updater.
# No reflash.
#
# Runs on an x86_64 (or arm64) Linux host via qemu-user emulation, and needs
# root (chroot + bind mounts). It deliberately reuses build-image.sh's proven
# qemu-chroot techniques (binfmt, the ld.so.preload/libarmmem shadow, the mount
# teardown) -- keep the two in sync if you touch those.
#
#   sudo scripts/build-chroot.sh bootstrap            # ONE TIME (long: downloads Pi OS + builds oF)
#   sudo scripts/build-chroot.sh build --version 1.4.0            # full: compile + bundle
#   sudo scripts/build-chroot.sh build --version 1.4.0+ui --frontend-only   # skip compile
#   sudo scripts/build-chroot.sh shell               # a shell inside the chroot (debug)
#   sudo scripts/build-chroot.sh clean               # delete the chroot
#
# Normally you drive this via scripts/deploy-update.sh, which also builds the
# web UI and can push+apply the result. Knobs:
#   LIVEPI_BUILD_ROOT   persistent chroot dir  (default /var/tmp/livepi-build-arm64)
#   LIVEPI_BASE_IMG_URL Pi OS base image       (default: Pi's _latest arm64 Lite)
# -E (errtrace): make the ERR trap below fire for failures INSIDE functions too
# -- without it a failing command in prep()/cmd_*() exits silently under set -e.
set -Eeuo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${LIVEPI_BUILD_ROOT:-/var/tmp/livepi-build-arm64}"
ROOTFS="$BUILD_ROOT/rootfs"
CACHE="$BUILD_ROOT/cache"
BASE_IMG_URL="${LIVEPI_BASE_IMG_URL:-https://downloads.raspberrypi.com/raspios_lite_arm64_latest}"
CHROOT_REPO="/build/livepi"          # repo path INSIDE the chroot
CHROOT_OF="/build/openFrameworks"    # oF tree setup-pi.sh installs beside it

log()  { printf '\n\033[1;36m[build-chroot]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[build-chroot] WARNING:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[build-chroot] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# Source-sync excludes: keep the chroot's arm64 artifacts (binary, .so's, pylib)
# and its persistent obj/, and never drag the host's x86 build over.
SYNC_EXCLUDES=(
    --exclude='.git/' --exclude='obj/' --exclude='frontend/node_modules/'
    --exclude='bin/livepi-video-glitch' --exclude='bin/*.so'
    --exclude='backend/pylib/' --exclude='backend/.venv/' --exclude='backend/.env'
    --exclude='__pycache__/' --exclude='*.pyc' --exclude='bin/data/config/app.local.json'
)

# --- chroot mount/prep (transient per invocation) --------------------------
MOUNTED=0
_umount_all() {
    local m
    for m in run sys proc dev/pts dev; do
        mountpoint -q "$ROOTFS/$m" && { umount "$ROOTFS/$m" 2>/dev/null || umount -l "$ROOTFS/$m" 2>/dev/null; }
    done
}
unprep() {
    set +e
    [[ -e "$ROOTFS/etc/ld.so.preload.livepi-bak" ]] && mv -f "$ROOTFS/etc/ld.so.preload.livepi-bak" "$ROOTFS/etc/ld.so.preload"
    _umount_all
    MOUNTED=0
}
trap '[[ "$MOUNTED" == 1 ]] && unprep' EXIT
# Loud failures: set -e otherwise exits silently, which looks like a hang.
trap 'rc=$?; printf "\033[1;31m[build-chroot] command failed (exit %s) near line %s\033[0m\n" "$rc" "${BASH_LINENO[0]:-?}" >&2' ERR

prep() {
    [[ -d "$ROOTFS" ]] || die "no build chroot at $ROOTFS -- run: sudo $0 bootstrap"
    _umount_all   # clear anything a killed run left behind
    # An unbooted rootfs can be missing these mount-point dirs; a mount onto a
    # non-existent target fails (and, pre-errtrace, exited silently).
    mkdir -p "$ROOTFS/dev/pts" "$ROOTFS/proc" "$ROOTFS/sys" "$ROOTFS/run"
    mount --bind /dev     "$ROOTFS/dev"
    mount --bind /dev/pts "$ROOTFS/dev/pts"
    mount -t proc  proc   "$ROOTFS/proc"
    mount -t sysfs sysfs  "$ROOTFS/sys"
    mount -t tmpfs tmpfs  "$ROOTFS/run"
    MOUNTED=1
    cp "$(command -v qemu-aarch64-static)" "$ROOTFS/usr/bin/" 2>/dev/null || true
    # Pi OS force-loads libarmmem via /etc/ld.so.preload; under emulation it
    # faults on every chroot command ("Illegal instruction"). Shadow it.
    if [[ -f "$ROOTFS/etc/ld.so.preload" && ! -e "$ROOTFS/etc/ld.so.preload.livepi-bak" ]]; then
        mv "$ROOTFS/etc/ld.so.preload" "$ROOTFS/etc/ld.so.preload.livepi-bak"
        : > "$ROOTFS/etc/ld.so.preload"
    fi
    cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf" 2>/dev/null || true
}

runch() { chroot "$ROOTFS" /usr/bin/env LC_ALL=C.UTF-8 LANG=C.UTF-8 "$@"; }

preflight() {
    [[ "$(id -u)" -eq 0 ]] || die "must run as root (chroot + bind mounts). Re-run with sudo."
    local missing=() t
    for t in curl xz rsync losetup parted chroot tar zstd sha256sum file python3; do
        command -v "$t" >/dev/null 2>&1 || missing+=("$t")
    done
    command -v qemu-aarch64-static >/dev/null 2>&1 || missing+=("qemu-aarch64-static")
    [[ ${#missing[@]} -eq 0 ]] || die "missing host tools: ${missing[*]}
  Debian/Ubuntu: sudo apt-get install qemu-user-static binfmt-support parted xz-utils rsync e2fsprogs zstd curl nodejs npm"
    if [[ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]]; then
        update-binfmts --enable qemu-aarch64 2>/dev/null || true
        [[ -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]] || \
            die "no aarch64 binfmt handler registered; enable qemu-user-static and retry."
    fi
}

# --- bootstrap: extract the arm64 rootfs + install the oF toolchain --------
cmd_bootstrap() {
    preflight
    mkdir -p "$CACHE" "$ROOTFS"
    local img="$CACHE/base.img" mnt="$BUILD_ROOT/basemnt"

    # Resumable: if the rootfs is already extracted (a prior run got this far),
    # skip the download + extract and pick up at the toolchain install.
    if [[ -f "$ROOTFS/etc/os-release" && -x "$ROOTFS/bin/bash" ]]; then
        log "reusing the already-extracted rootfs at $ROOTFS (skipping download + extract)"
    else
        local xz="$CACHE/base.img.xz"
        if [[ ! -f "$xz" ]]; then
            log "downloading base Pi OS image (cached at $xz)"
            curl -fL --retry 3 -o "$xz.part" "$BASE_IMG_URL"; mv "$xz.part" "$xz"
        else
            log "using cached base image $xz"
        fi
        # Clear any loop/mount a previous failed run leaked on this base image.
        mountpoint -q "$mnt" && umount "$mnt" 2>/dev/null || true
        local stale; stale="$(losetup -j "$img" 2>/dev/null | cut -d: -f1)"
        [[ -n "$stale" ]] && losetup -d "$stale" 2>/dev/null || true

        log "decompressing the base image"
        xz -dc "$xz" > "$img"
        local loop; loop="$(losetup -f --show -P "$img")"
        udevadm settle 2>/dev/null || sleep 1
        [[ -b "${loop}p2" ]] || { losetup -d "$loop"; die "unexpected base image layout (no ${loop}p2)"; }
        mkdir -p "$mnt"; mount "${loop}p2" "$mnt"
        log "copying the rootfs into $ROOTFS (~2GB, a few minutes)"
        # -a only, NOT -HAX: a build chroot needs the files, not ACLs/xattrs, and
        # -X/-A trip non-fatal errors (rsync code 23) on a system rootfs that
        # would abort the whole script under set -e. Tolerate the benign codes.
        rsync -a --numeric-ids --info=progress2 "$mnt/" "$ROOTFS/" || {
            local rc=$?; [[ $rc -eq 23 || $rc -eq 24 ]] \
                || die "rsync failed (code $rc) copying the rootfs"
            warn "rsync reported code $rc (a few attrs/files skipped) -- fine for a build chroot"
        }
        umount "$mnt"; losetup -d "$loop"; rmdir "$mnt" 2>/dev/null || true
        rm -f "$img"
    fi
    mkdir -p "$ROOTFS/boot/firmware" "$ROOTFS$CHROOT_REPO" "$ROOTFS/build"

    prep
    log "syncing repo -> chroot $CHROOT_REPO"
    rsync -a --delete "${SYNC_EXCLUDES[@]}" "$REPO_DIR/" "$ROOTFS$CHROOT_REPO/"

    # Unattended apt for the emulated build: oF's install_dependencies.sh (via
    # setup-pi.sh) shells out to apt WITHOUT -y under sudo, which resets the env
    # and drops DEBIAN_FRONTEND, so it would block on "continue? [Y/n]". Mirror
    # provision-appliance.sh's build-time apt policy (this is a build chroot, so
    # the assume-yes drop-in can simply stay). Then refresh the package lists.
    install -D -m 644 /dev/stdin "$ROOTFS/etc/apt/apt.conf.d/90livepi-build-unattended" <<'APTCFG'
APT::Get::Assume-Yes "true";
Dpkg::Options { "--force-confdef"; "--force-confold"; };
APTCFG
    runch bash -c 'echo "debconf debconf/frontend select Noninteractive" | debconf-set-selections 2>/dev/null || true'
    log "refreshing apt package lists in the chroot"
    runch apt-get update || warn "apt-get update failed in the chroot (network/DNS?) -- setup-pi may still work from cache"

    # setup-pi.sh installs oF + every build dep. Same script build-image's
    # provision runs (a validated path) -- here just the compile subset.
    log "installing the openFrameworks toolchain (LONG -- downloads + builds oF under emulation)"
    runch env OF_ROOT="$CHROOT_OF" DEBIAN_FRONTEND=noninteractive \
        bash "$CHROOT_REPO/scripts/setup-pi.sh" || die "setup-pi.sh failed in the chroot"
    log "initial renderer compile (warms the persistent obj/ -- also long the first time)"
    runch make -C "$CHROOT_REPO" OF_ROOT="$CHROOT_OF" -j"$(nproc)" || die "initial make failed"
    log "backend pylib (arm64 wheels)"
    runch bash -c "python3 -m pip install -q --target '$CHROOT_REPO/backend/pylib' \
        --break-system-packages -r '$CHROOT_REPO/backend/requirements.txt'" \
        || warn "pylib install failed -- backend-changing updates need it; fix + re-run, or install by hand via '$0 shell'"
    unprep
    log "bootstrap DONE. Build a bundle with:  scripts/deploy-update.sh --version <v>"
}

# --- build: incremental compile + emit a bundle ----------------------------
cmd_build() {
    preflight
    [[ -d "$ROOTFS$CHROOT_REPO" ]] || die "chroot not bootstrapped -- run: sudo $0 bootstrap"
    local version="" out="$PWD" skip_make=0
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --version)       version="$2"; shift 2 ;;
            --out)           out="$2"; shift 2 ;;
            --frontend-only) skip_make=1; shift ;;
            *) die "unknown build arg: $1" ;;
        esac
    done

    prep
    log "syncing repo source -> chroot (obj/ + arm64 artifacts kept for incremental build)"
    rsync -a --delete "${SYNC_EXCLUDES[@]}" "$REPO_DIR/" "$ROOTFS$CHROOT_REPO/"
    if [[ "$skip_make" == 0 ]]; then
        log "compiling the renderer (incremental)"
        runch make -C "$CHROOT_REPO" OF_ROOT="$CHROOT_OF" -j"$(nproc)" || die "make failed"
        if [[ ! -f "$ROOTFS$CHROOT_REPO/backend/pylib/uvicorn/__init__.py" ]]; then
            log "backend pylib missing -> installing"
            runch bash -c "python3 -m pip install -q --target '$CHROOT_REPO/backend/pylib' \
                --break-system-packages -r '$CHROOT_REPO/backend/requirements.txt'" || warn "pylib install failed"
        fi
    else
        log "--frontend-only: skipping the renderer compile (reusing the chroot's arm64 binary)"
    fi
    unprep

    # Bundle on the HOST: tar/zstd run native (fast) against the arm64 tree the
    # chroot built. build-bundle checks the binary is aarch64 -- it is.
    log "assembling the bundle (host-side) from the arm64 tree"
    mkdir -p "$out"
    local args=(--tree "$ROOTFS$CHROOT_REPO" --out "$out")
    [[ -n "$version" ]] && args+=(--version "$version")
    bash "$REPO_DIR/scripts/build-bundle.sh" "${args[@]}"
    # Hand the emitted bundle back to whoever sudo'd us (deploy-update reads it).
    if [[ -n "${SUDO_USER:-}" ]] && id "$SUDO_USER" >/dev/null 2>&1; then
        chown -R "$SUDO_USER:$(id -gn "$SUDO_USER")" "$out" 2>/dev/null || true
    fi
}

case "${1:-}" in
    bootstrap) shift; cmd_bootstrap "$@" ;;
    build)     shift; cmd_build "$@" ;;
    shell)     preflight; prep; runch /bin/bash || true; unprep ;;
    clean)     [[ "$(id -u)" -eq 0 ]] || die "run as root"; _umount_all; rm -rf "$BUILD_ROOT"; log "removed $BUILD_ROOT" ;;
    *) echo "usage: build-chroot.sh {bootstrap|build [--version V] [--out DIR] [--frontend-only]|shell|clean}" >&2; exit 2 ;;
esac
