#!/usr/bin/env bash
# First-boot DATA-partition preparation for a shipped LivePi box. Runs ONCE,
# as root, from livepi-dataprep.service on the very first boot -- BEFORE
# livepi-firstboot (which writes the per-device secrets into /data) and before
# the backend/kiosk (which read from it). See docs/distribution.md
# "Filesystem & partitions".
#
# WHY this exists: the read-only-root appliance keeps all writable user content
# on a SEPARATE partition (LABEL=LIVEPI_DATA, mounted /data) so it survives the
# overlay that makes the rest of the card read-only (docs/architecture.md
# "Data model & read-only root"). The shipped image is a plain two-partition
# Pi OS (boot + rootfs) with the rest of the card left unpartitioned; this
# script claims that free space as /data on first boot. That's deliberately the
# same trick Raspberry Pi OS's own init_resize uses to grow rootfs to fill a
# card -- except we ADD a trailing partition rather than grow an existing one,
# which is strictly safer (no in-place filesystem move of a mounted volume).
#
# The build DISABLES Pi OS's stock rootfs auto-grow (resize2fs_once) precisely
# so that free space is still here for us to claim -- see scripts/build-image.sh.
#
# Everything is idempotent: the partition labelled LIVEPI_DATA existing is the
# single source of truth for "done". A reboot re-runs the oneshot; it no-ops.
#
# Dry-run off a Pi (prints the destructive commands instead of running them):
#   LIVEPI_DATAPREP_DRYRUN=1 ./scripts/dataprep.sh
set -euo pipefail

DATA_DIR="${LIVEPI_DATA_DIR:-/data}"
DATA_LABEL="${LIVEPI_DATA_LABEL:-LIVEPI_DATA}"
DRYRUN="${LIVEPI_DATAPREP_DRYRUN:-0}"

log() { printf '[dataprep] %s\n' "$*"; }
# run: execute, or just echo when dry-running (for the mutating commands only).
run() {
    if [[ "$DRYRUN" == "1" ]]; then
        printf '[dataprep:dryrun] %s\n' "$*"
    else
        "$@"
    fi
}

FSTAB_LINE="LABEL=$DATA_LABEL  $DATA_DIR  ext4  defaults,noatime,nofail  0  2"

# Ensure /etc/fstab mounts the data partition by label (nofail: a box that
# somehow lost the partition still boots to a usable, if data-less, state; the
# overlay makes /etc writable-but-volatile after lockdown, but this line is
# baked into the read-only lower layer here on the first, still-writable boot).
ensure_fstab() {
    if grep -qE "LABEL=$DATA_LABEL[[:space:]]" /etc/fstab 2>/dev/null; then
        return 0
    fi
    log "adding $DATA_DIR to /etc/fstab"
    if [[ "$DRYRUN" == "1" ]]; then
        printf '[dataprep:dryrun] append to /etc/fstab: %s\n' "$FSTAB_LINE"
    else
        printf '%s\n' "$FSTAB_LINE" >> /etc/fstab
    fi
}

# The backend self-seeds its default show; ingest writes under clips/. We just
# make the tree exist. (firstboot.sh also does this, but dataprep runs first
# and mounts the volume, so seed here too -- both are idempotent mkdir -p.)
seed_tree() {
    for d in shows clips clips/.thumbs clips/.pingpong config backend network; do
        run mkdir -p "$DATA_DIR/$d"
    done
}

# --- already provisioned? ---------------------------------------------------
# blkid -L returns the device for a label if it exists anywhere on the system.
if EXISTING="$(blkid -L "$DATA_LABEL" 2>/dev/null)"; then
    log "$DATA_LABEL already exists at $EXISTING -- ensuring it's mounted"
    ensure_fstab
    if ! mountpoint -q "$DATA_DIR"; then
        run mkdir -p "$DATA_DIR"
        run mount "$DATA_DIR" || run mount "$EXISTING" "$DATA_DIR"
    fi
    seed_tree
    run touch "$DATA_DIR/.dataprep-done"
    log "done (existing data partition)"
    exit 0
fi

log "no $DATA_LABEL partition yet -- claiming the card's free space for $DATA_DIR"

# --- locate the disk and the next partition slot ---------------------------
# The root filesystem's block device -> its parent whole disk. At this point
# (first boot, pre-lockdown) the overlay is off, so / is the real rootfs
# partition, e.g. /dev/mmcblk0p2 -> disk /dev/mmcblk0.
ROOT_SRC="$(findmnt -no SOURCE / 2>/dev/null || true)"
if [[ -z "$ROOT_SRC" || ! -b "$ROOT_SRC" ]]; then
    log "ERROR: could not resolve the root block device (got '$ROOT_SRC'); aborting"
    exit 1
fi
DISK_NAME="$(lsblk -no pkname "$ROOT_SRC" | head -n1)"
if [[ -z "$DISK_NAME" ]]; then
    log "ERROR: could not find the parent disk of $ROOT_SRC; aborting"
    exit 1
fi
DISK="/dev/$DISK_NAME"

# Highest existing partition number -> the new one is +1. lsblk's PARTN column
# reads the number straight from the partition table and (unlike partx) needs
# no raw-disk read privilege; the disk's own row has an empty PARTN, so filter
# to digit rows. `|| true` keeps an empty result from tripping pipefail before
# the guard below can report it.
LAST_NUM="$(lsblk -nro PARTN "$DISK" 2>/dev/null | grep -E '^[0-9]+$' | sort -n | tail -n1 || true)"
if [[ -z "$LAST_NUM" ]]; then
    log "ERROR: could not read the partition table of $DISK; aborting"
    exit 1
fi
NEW_NUM=$((LAST_NUM + 1))

# Partition device naming: mmcblk0/nvme0n1 (name ends in a digit) interpose a
# 'p' before the number; sd*/vd* (name ends in a letter) do not.
case "$DISK" in
    *[0-9]) PART_DEV="${DISK}p${NEW_NUM}" ;;
    *)      PART_DEV="${DISK}${NEW_NUM}" ;;
esac

log "disk=$DISK  root=$ROOT_SRC  new partition=$PART_DEV (#$NEW_NUM)"

# --- append the partition using all remaining free space -------------------
# `;` on stdin is one sfdisk line with every field defaulted: start = first
# aligned sector after the last partition, size = rest of the disk, type =
# Linux. --append leaves partitions 1..N untouched; --no-reread is required
# because the kernel refuses to re-read a table whose partitions are mounted;
# --force proceeds past the "device in use" warning that follows from that.
log "appending partition..."
if [[ "$DRYRUN" == "1" ]]; then
    printf '[dataprep:dryrun] echo ";" | sfdisk --append --no-reread --force %s\n' "$DISK"
else
    echo ';' | sfdisk --append --no-reread --force "$DISK"
fi

# Tell the running kernel about JUST the new partition (a full re-read would
# fail on the mounted rootfs). partx -a adds partitions the kernel doesn't
# know yet; -u updates if it somehow already saw it.
run partx -a --nr "$NEW_NUM" "$DISK" || run partx -u "$DISK" || true
run udevadm settle || true

# Wait for the device node (udev can lag the kernel a beat).
if [[ "$DRYRUN" != "1" ]]; then
    for _ in $(seq 1 30); do
        [[ -b "$PART_DEV" ]] && break
        sleep 0.2
    done
    if [[ ! -b "$PART_DEV" ]]; then
        log "ERROR: $PART_DEV never appeared after partitioning; aborting"
        exit 1
    fi
fi

# --- format, mount, wire up ------------------------------------------------
log "formatting $PART_DEV as ext4 (label $DATA_LABEL)"
run mkfs.ext4 -q -L "$DATA_LABEL" -F "$PART_DEV"

run mkdir -p "$DATA_DIR"
log "mounting $PART_DEV at $DATA_DIR"
run mount "$PART_DEV" "$DATA_DIR"

ensure_fstab
seed_tree
run touch "$DATA_DIR/.dataprep-done"

log "done: $DATA_DIR is a $DATA_LABEL partition on $PART_DEV"
