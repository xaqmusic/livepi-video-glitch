#!/usr/bin/env bash
# Build the flashable LivePi golden image from stock Raspberry Pi OS Lite
# (arm64, Trixie), on an x86_64 (or arm64) Linux host via qemu-user emulation.
# Downloads the base image, loop-mounts it, chroots in, runs the shared
# provisioner (scripts/provision-appliance.sh) to turn it into a LivePi
# appliance, and repacks to livepi-<version>.img.xz. See docs/distribution.md
# "Build pipeline" and docs/deploy.md.
#
# MUST run as root (loop devices, mounts, chroot). On a Debian/Ubuntu host:
#     sudo apt-get install qemu-user-static binfmt-support parted \
#                          xz-utils rsync e2fsprogs dosfstools nodejs npm
# Then:
#     sudo ./scripts/build-image.sh            # full build
#     sudo ./scripts/build-image.sh --check    # preflight only (no build)
#
# Knobs (env, with defaults):
#     LIVEPI_BASE_IMG_URL   stock image (default: Pi's _latest arm64 Lite)
#     LIVEPI_APP_DIR=/opt/livepi        app tree location on the image
#     LIVEPI_APP_USER=pi                account the services run as
#     LIVEPI_DATA_DIR=/data             writable data-partition mountpoint
#     LIVEPI_LOCKDOWN=1                 1 = ship (RO root after 1st boot), 0 = dev
#     LIVEPI_WIFI_COUNTRY=US            regdomain for the control AP
#     LIVEPI_ENLARGE_MB=4096            headroom added to rootfs for the build
#     LIVEPI_PREBUILT=0                 1 = inject a prebuilt binary (skip compile)
#     LIVEPI_WORK_DIR=/var/tmp/livepi-image      scratch + base-image cache
#     LIVEPI_OUTPUT_DIR=$LIVEPI_WORK_DIR         where the .img.xz lands
#     LIVEPI_VERSION=$(git describe)             image version tag
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BASE_IMG_URL="${LIVEPI_BASE_IMG_URL:-https://downloads.raspberrypi.com/raspios_lite_arm64_latest}"
APP_DIR="${LIVEPI_APP_DIR:-/opt/livepi}"
APP_USER="${LIVEPI_APP_USER:-pi}"
DATA_DIR="${LIVEPI_DATA_DIR:-/data}"
LOCKDOWN="${LIVEPI_LOCKDOWN:-1}"
WIFI_COUNTRY="${LIVEPI_WIFI_COUNTRY:-US}"
ENLARGE_MB="${LIVEPI_ENLARGE_MB:-4096}"
PREBUILT="${LIVEPI_PREBUILT:-0}"
WORK_DIR="${LIVEPI_WORK_DIR:-/var/tmp/livepi-image}"
OUTPUT_DIR="${LIVEPI_OUTPUT_DIR:-$WORK_DIR}"
VERSION="${LIVEPI_VERSION:-$(git -C "$REPO_DIR" describe --tags --always --dirty 2>/dev/null || echo dev)}"

CACHE_DIR="$WORK_DIR/cache"
BUILD_IMG="$WORK_DIR/livepi-build.img"
MNT="$WORK_DIR/mnt"

log()  { printf '\n\033[1;36m[build-image]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[build-image] WARNING:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[build-image] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# --- cleanup trap ----------------------------------------------------------
# Everything mutable is unwound here so a failure (or success) never leaves a
# dangling loop device or bind mount behind. Idempotent + best-effort.
LOOP=""
cleanup() {
    local rc=$?
    set +e
    # Restore the files we shadowed inside the image.
    if [[ -e "$MNT/etc/ld.so.preload.livepi-bak" ]]; then
        mv -f "$MNT/etc/ld.so.preload.livepi-bak" "$MNT/etc/ld.so.preload"
    fi
    [[ -e "$MNT/etc/resolv.conf.livepi-bak" ]] && mv -f "$MNT/etc/resolv.conf.livepi-bak" "$MNT/etc/resolv.conf"
    rm -f "$MNT/usr/bin/qemu-aarch64-static" 2>/dev/null
    # Unmount in reverse dependency order; lazy-unmount as a fallback.
    local m
    for m in run sys proc dev/pts dev boot/firmware ''; do
        mountpoint -q "$MNT/$m" && { umount "$MNT/$m" 2>/dev/null || umount -l "$MNT/$m" 2>/dev/null; }
    done
    [[ -n "$LOOP" ]] && losetup -d "$LOOP" 2>/dev/null
    return $rc
}
trap cleanup EXIT

# --- preflight -------------------------------------------------------------
preflight() {
    log "preflight"
    [[ "$(id -u)" -eq 0 ]] || die "must run as root (loop devices, mount, chroot). Re-run with sudo."

    local missing=()
    local tool
    for tool in curl xz rsync losetup parted mkfs.ext4 e2fsck resize2fs partprobe chroot; do
        command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
    done
    # qemu-aarch64-static: the interpreter that runs arm64 binaries in the chroot.
    local qemu; qemu="$(command -v qemu-aarch64-static || true)"
    [[ -n "$qemu" ]] || missing+=("qemu-aarch64-static")
    if [[ ${#missing[@]} -gt 0 ]]; then
        die "missing host tools: ${missing[*]}
  Debian/Ubuntu: sudo apt-get install qemu-user-static binfmt-support parted xz-utils rsync e2fsprogs dosfstools coreutils"
    fi

    # binfmt_misc must know how to run aarch64 ELF, or the chroot's first arm64
    # command dies with "Exec format error".
    if [[ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]]; then
        warn "binfmt for aarch64 not registered. Enabling qemu-user-static's binfmts, or run:
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
  (also: sudo update-binfmts --enable qemu-aarch64)"
        update-binfmts --enable qemu-aarch64 2>/dev/null || true
        [[ -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]] || \
            die "still no aarch64 binfmt handler; register qemu-user-static and retry."
    fi

    # The web UI is built on the host (node), then rsynced in -- verify we can.
    if [[ ! -f "$REPO_DIR/frontend/dist/index.html" ]]; then
        command -v npm >/dev/null 2>&1 || \
            die "frontend/dist is not built and npm is not installed. Install nodejs+npm, or build frontend/dist first."
    fi
    log "preflight OK  (version=$VERSION, base=$BASE_IMG_URL)"
}

# --- host-side frontend build ----------------------------------------------
# ALWAYS rebuild by default: a stale frontend/dist left on the build machine
# would silently ship a UI that doesn't match the committed source (this bit us
# -- a PasswordDialog fix never reached the image). Skip only when explicitly
# asked, and only if a dist already exists.
build_frontend() {
    if [[ "${LIVEPI_SKIP_FRONTEND:-0}" == "1" && -f "$REPO_DIR/frontend/dist/index.html" ]]; then
        log "LIVEPI_SKIP_FRONTEND=1 -- reusing existing frontend/dist (unchanged UI)"
        return
    fi
    log "building the web UI on the host (rebuilt every image; LIVEPI_SKIP_FRONTEND=1 to reuse)"
    ( cd "$REPO_DIR/frontend" && npm ci && npm run build ) || die "frontend build failed"
    [[ -f "$REPO_DIR/frontend/dist/index.html" ]] || die "web build did not produce frontend/dist/index.html"
}

# --- fetch + decompress the base image -------------------------------------
fetch_base() {
    mkdir -p "$CACHE_DIR"
    local xz="$CACHE_DIR/base.img.xz"
    if [[ ! -f "$xz" ]]; then
        log "downloading base image (cached at $xz)"
        curl -fL --retry 3 -o "$xz.part" "$BASE_IMG_URL"
        mv "$xz.part" "$xz"
    else
        log "using cached base image $xz"
    fi
    log "decompressing base -> $BUILD_IMG"
    xz -dc "$xz" > "$BUILD_IMG"
}

# --- enlarge the image + grow rootfs for the build -------------------------
enlarge() {
    log "enlarging rootfs by ${ENLARGE_MB}MB for the build"
    truncate -s "+${ENLARGE_MB}M" "$BUILD_IMG"
    LOOP="$(losetup -f --show -P "$BUILD_IMG")"
    udevadm settle 2>/dev/null || sleep 1   # partition nodes can lag losetup -P
    [[ -b "${LOOP}p2" ]] || die "expected ${LOOP}p2 after losetup -P; base image layout unexpected"
    # Grow partition 2 to the end of the now-larger disk, then its filesystem.
    parted -s "$LOOP" resizepart 2 100%
    partprobe "$LOOP"
    e2fsck -pf "${LOOP}p2" || true   # -p auto-fixes; rc 1 = fixed, still ok
    resize2fs "${LOOP}p2"
}

# --- mount + prepare the chroot --------------------------------------------
mount_chroot() {
    log "mounting the image + preparing the chroot"
    mkdir -p "$MNT"
    mount "${LOOP}p2" "$MNT"
    # Trixie's boot partition mounts at /boot/firmware.
    mkdir -p "$MNT/boot/firmware"
    mount "${LOOP}p1" "$MNT/boot/firmware"

    mount --bind /dev     "$MNT/dev"
    mount --bind /dev/pts "$MNT/dev/pts"
    mount -t proc  proc   "$MNT/proc"
    mount -t sysfs sysfs  "$MNT/sys"
    mount -t tmpfs tmpfs  "$MNT/run"

    # The qemu interpreter, in case binfmt isn't the fix-binary (F) flavour.
    cp "$(command -v qemu-aarch64-static)" "$MNT/usr/bin/"

    # CRUCIAL qemu-chroot fix: Pi OS's /etc/ld.so.preload force-loads libarmmem
    # (a Pi-CPU memcpy). Under emulation that faults on EVERY command run in the
    # chroot ("Illegal instruction"). Shadow it for the duration.
    if [[ -f "$MNT/etc/ld.so.preload" ]]; then
        mv "$MNT/etc/ld.so.preload" "$MNT/etc/ld.so.preload.livepi-bak"
        : > "$MNT/etc/ld.so.preload"
    fi
    # DNS for apt/curl/pip inside the chroot.
    cp "$MNT/etc/resolv.conf" "$MNT/etc/resolv.conf.livepi-bak" 2>/dev/null || true
    cp -L /etc/resolv.conf "$MNT/etc/resolv.conf"
}

# --- place the repo + run the provisioner ----------------------------------
provision() {
    log "syncing the repo into $APP_DIR (via .rsyncfilter)"
    mkdir -p "$MNT$APP_DIR"
    rsync -a --delete --filter="merge $REPO_DIR/.rsyncfilter" "$REPO_DIR/" "$MNT$APP_DIR/"

    log "running provision-appliance.sh inside the chroot (this is the long part)"
    chroot "$MNT" /usr/bin/env \
        LC_ALL=C.UTF-8 LANG=C.UTF-8 \
        LIVEPI_APP_DIR="$APP_DIR" \
        LIVEPI_APP_USER="$APP_USER" \
        LIVEPI_DATA_DIR="$DATA_DIR" \
        LIVEPI_LOCKDOWN="$LOCKDOWN" \
        LIVEPI_WIFI_COUNTRY="$WIFI_COUNTRY" \
        LIVEPI_PREBUILT="$PREBUILT" \
        /bin/bash "$APP_DIR/scripts/provision-appliance.sh"
}

# --- unmount, repack, checksum ---------------------------------------------
finalize() {
    log "unmounting + detaching"
    cleanup             # runs the trap body now (restores files, unmounts, frees loop)
    trap - EXIT
    LOOP=""

    mkdir -p "$OUTPUT_DIR"
    local out="$OUTPUT_DIR/livepi-$VERSION.img"
    mv "$BUILD_IMG" "$out"
    log "compressing -> $out.xz (this takes a while)"
    xz -T0 -f "$out"
    ( cd "$OUTPUT_DIR" && sha256sum "livepi-$VERSION.img.xz" > "livepi-$VERSION.img.xz.sha256" )

    log "DONE"
    printf '  image:  %s.xz\n  sha256: %s\n\n' "$out" "$OUTPUT_DIR/livepi-$VERSION.img.xz.sha256"
    printf '  Flash with Raspberry Pi Imager / balenaEtcher, then boot. First boot:\n'
    printf '    dataprep (claim card free space -> /data) -> firstboot (per-device\n'
    printf '    secrets + control AP) -> services -> lockdown (read-only root) -> reboot.\n'
}

# --- main ------------------------------------------------------------------
preflight
if [[ "${1:-}" == "--check" ]]; then
    log "--check: preflight only, not building."
    trap - EXIT
    exit 0
fi
build_frontend
fetch_base
enlarge
mount_chroot
provision
finalize
