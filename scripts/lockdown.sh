#!/usr/bin/env bash
# Final first-boot step for a shipped LivePi box: flip the root filesystem to
# read-only and reboot into the locked-down appliance state. Runs ONCE, as
# root, from livepi-lockdown.service AFTER livepi-firstboot has personalized
# the box (secrets, hostname, control AP). Last link in the first-boot chain:
#   dataprep -> firstboot -> (backend + kiosk) -> LOCKDOWN (this) -> reboot.
# See docs/distribution.md "Filesystem & partitions" and docs/architecture.md
# "Data model & read-only root".
#
# WHY a separate reboot instead of shipping the overlay pre-enabled: the
# overlay makes ALL of / volatile, so the personalization firstboot writes to
# /etc (hostname, the NetworkManager AP profile) would evaporate on the next
# boot. So the box must come up writable ONCE, personalize, and only THEN lock
# down -- which is exactly this unit. Everything a customer changes afterward
# (joined venue WiFi, uploaded clips, the changed password) lands on the
# separate writable /data partition, never the frozen root.
#
# This is the scariest unit in the system: it runs unattended and reboots. It
# is therefore strictly fail-safe -- every step is checked, and if ANY of them
# fails it logs and exits WITHOUT rebooting, leaving a writable, debuggable box
# rather than a half-locked one.
#
# Set LIVEPI_LOCKDOWN=0 to make it a no-op (used for dev/test images that must
# stay writable). The image build sets it to 1 only for shipping images.
set -euo pipefail

DATA_DIR="${LIVEPI_DATA_DIR:-/data}"
DO_LOCKDOWN="${LIVEPI_LOCKDOWN:-1}"
NM_CONN_DIR="/etc/NetworkManager/system-connections"
NM_CONN_PERSIST="$DATA_DIR/network/system-connections"
CMDLINE="/boot/firmware/cmdline.txt"
[[ -f "$CMDLINE" ]] || CMDLINE="/boot/cmdline.txt"   # pre-Bookworm layout

log() { printf '[lockdown] %s\n' "$*"; }
fail() { printf '[lockdown] ERROR: %s -- NOT locking down; box stays writable\n' "$*" >&2; exit 1; }

# --- opt-out (dev/test images) ---------------------------------------------
if [[ "$DO_LOCKDOWN" != "1" ]]; then
    log "LIVEPI_LOCKDOWN=$DO_LOCKDOWN -- skipping overlay + reboot (writable dev image)"
    exit 0
fi

# --- already locked down? ---------------------------------------------------
# cmdline.txt carrying the overlay marker is the source of truth: the overlay is
# either active now or will be on the next boot. Trixie's raspi-config writes
# `overlayroot=tmpfs` (the overlayroot package); older ones wrote `boot=overlay`
# -- accept either, or lockdown re-runs every boot and fails on the NM copy.
if grep -qE 'boot=overlay|overlayroot=' "$CMDLINE" 2>/dev/null; then
    log "overlay already enabled ($CMDLINE) -- nothing to do"
    exit 0
fi

# --- refuse to run before firstboot finished -------------------------------
# The unit also guards this with ConditionPathExists, but double-check: locking
# down a box whose /etc personalization hasn't happened would freeze the wrong
# state.
[[ -f "$DATA_DIR/.provisioned" ]] || fail "firstboot has not completed ($DATA_DIR/.provisioned missing)"

log "personalization complete -- preparing to lock the root filesystem read-only"

# --- persist NetworkManager connections onto /data -------------------------
# On the read-only root, /etc is a volatile tmpfs overlay, so any venue-WiFi
# network the customer joins later (nmcli writes a .nmconnection under
# $NM_CONN_DIR) would be lost on reboot. Relocate that directory onto the
# writable /data partition via a bind mount so joined networks -- and the
# control AP firstboot just created -- persist across reboots and power cuts.
persist_nm_connections() {
    install -d -m 0700 -o root -g root "$NM_CONN_PERSIST" \
        || fail "could not create $NM_CONN_PERSIST"
    # Seed it with whatever already exists (the AP profile, at least), matching
    # NM's required 0700 dir / 0600 file perms. cp -a is a no-op if the source
    # is empty.
    # Skip once the fstab bind is already active (a re-run): $NM_CONN_DIR is then
    # a mountpoint of the persist dir itself, and cp errors copying a directory
    # onto itself (the actual failure seen on hardware).
    if ! mountpoint -q "$NM_CONN_DIR" && compgen -G "$NM_CONN_DIR/*" >/dev/null 2>&1; then
        cp -a "$NM_CONN_DIR/." "$NM_CONN_PERSIST/" \
            || fail "could not copy existing NM connections to $NM_CONN_PERSIST"
    fi
    # Bind-mount it over the (soon-to-be-volatile) /etc location on every boot.
    # requires-mounts-for=/data orders it after the LIVEPI_DATA mount; NM starts
    # well after local-fs.target, so the bind is in place before NM reads it.
    local line="$NM_CONN_PERSIST $NM_CONN_DIR none bind,x-systemd.requires-mounts-for=$DATA_DIR 0 0"
    if ! grep -qF "$NM_CONN_PERSIST $NM_CONN_DIR" /etc/fstab 2>/dev/null; then
        printf '%s\n' "$line" >> /etc/fstab || fail "could not add the NM bind mount to /etc/fstab"
        log "NetworkManager connections now persist on $NM_CONN_PERSIST"
    fi
}
persist_nm_connections

# --- enable the read-only overlay ------------------------------------------
# raspi-config's non-interactive overlayfs toggle: builds an initramfs with the
# overlay, adds `initramfs` to config.txt and `boot=overlay` to cmdline.txt.
# Takes effect on the next boot (hence the reboot below). Boot partition stays
# read-write for v1 (config.txt/firmware updates); rootfs -- where an unclean
# shutdown mid-write would corrupt the OS or the app -- is what this protects.
command -v raspi-config >/dev/null 2>&1 || fail "raspi-config not found (cannot enable overlayfs)"
log "enabling overlayfs via raspi-config..."
if ! raspi-config nonint enable_overlayfs; then
    fail "raspi-config nonint enable_overlayfs failed"
fi
grep -qE 'boot=overlay|overlayroot=' "$CMDLINE" 2>/dev/null \
    || fail "enable_overlayfs returned success but $CMDLINE has no overlay marker"

# CRUCIAL: raspi-config writes a bare `overlayroot=tmpfs`, and overlayroot's
# default (recurse=1) then overlays EVERY mount, not just / -- including the
# separate writable LIVEPI_DATA partition. That wraps /data in a volatile tmpfs
# upper (real partition demoted to a read-only lower), so everything the box
# writes there -- shows, clips, settings, auth.json, the per-device secrets, the
# .claimed marker, joined-WiFi profiles -- evaporates on the next reboot. Exactly
# the opposite of the persistent-/data design. recurse=0 confines the overlay to
# / alone, leaving /data (and any other fstab mount) as its real, writable,
# persistent self; /etc/overlayroot.conf documents this very case ("separate
# partition ... recurse set to 0"). The kernel cmdline overlayroot= overrides
# overlayroot.conf, so it must be patched here, where enable_overlayfs wrote it.
if grep -q 'overlayroot=tmpfs' "$CMDLINE" && ! grep -q 'recurse=0' "$CMDLINE"; then
    sed -i 's/overlayroot=tmpfs/overlayroot=tmpfs:recurse=0/' "$CMDLINE"
    grep -q 'overlayroot=tmpfs:recurse=0' "$CMDLINE" \
        || fail "could not set overlayroot recurse=0 in $CMDLINE (/data would be volatile)"
    log "overlayroot: confined the overlay to / (recurse=0); /data stays persistent"
fi

log "root filesystem will be read-only after reboot. Rebooting into the locked-down appliance now."
# Give the journal a moment to flush the above (journald is volatile, so this
# is only for anyone watching the console live).
sync
systemctl reboot
