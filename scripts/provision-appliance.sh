#!/usr/bin/env bash
# Turn "this root filesystem" into a shipping LivePi appliance: build the app,
# bake the X11 kiosk, install + enable every LivePi systemd unit, and prime the
# read-only-root / per-device-secrets first-boot chain. Runs as root.
#
# Deliberately PATH-AGNOSTIC -- the exact same script provisions either:
#   * a qemu chroot over a stock Pi OS image  (scripts/build-image.sh), or
#   * a real Raspberry Pi, natively, which is then captured to an image.
# It therefore only ever *enables* units (a static, offline-safe symlink op
# that works fine in a chroot) and never *starts* them, and it tolerates the
# live-only operations (rfkill, dbus) that a chroot can't do.
#
# It assumes the repo has already been placed at $LIVEPI_APP_DIR (build-image.sh
# rsyncs it in; on a Pi you'd clone/rsync it there yourself). See
# docs/distribution.md "Build pipeline" and docs/deploy.md.
#
# Knobs (all optional; shown with defaults):
#   LIVEPI_APP_DIR=/opt/livepi        where the app tree lives (read-only root)
#   LIVEPI_APP_USER=pi                account the backend + kiosk run as
#   LIVEPI_DATA_DIR=/data             writable user-data partition mountpoint
#   LIVEPI_OF_ROOT=<app_dir>/../openFrameworks   oF build tree (setup-pi default)
#   LIVEPI_LOCKDOWN=1                 1 = ship (lock RO after first boot), 0 = dev
#   LIVEPI_WIFI_COUNTRY=US            regdomain so the control AP can broadcast
#   LIVEPI_KEEP_OF=0                  1 keeps the oF build tree on the image
#   LIVEPI_PREBUILT=0                 1 = binary+venv already present, skip build
set -euo pipefail

APP_DIR="${LIVEPI_APP_DIR:-/opt/livepi}"
APP_USER="${LIVEPI_APP_USER:-pi}"
DATA_DIR="${LIVEPI_DATA_DIR:-/data}"
OF_ROOT="${LIVEPI_OF_ROOT:-$(dirname "$APP_DIR")/openFrameworks}"
LOCKDOWN="${LIVEPI_LOCKDOWN:-1}"
WIFI_COUNTRY="${LIVEPI_WIFI_COUNTRY:-US}"
KEEP_OF="${LIVEPI_KEEP_OF:-0}"
PREBUILT="${LIVEPI_PREBUILT:-0}"

log() { printf '\n[provision] === %s ===\n' "$*"; }
warn() { printf '[provision] WARNING: %s\n' "$*" >&2; }

[[ "$(id -u)" -eq 0 ]] || { echo "must run as root" >&2; exit 1; }
[[ -d "$APP_DIR" ]] || { echo "app tree not found at $APP_DIR (rsync the repo there first)" >&2; exit 1; }

export DEBIAN_FRONTEND=noninteractive

# Appliance debconf posture: NEVER prompt, permanently. The read-only overlay
# can't persist a debconf answer, so ANY package that defers configuration to
# first boot would pop its "Package configuration" dialog on the console EVERY
# boot. Bake the frontend to noninteractive into the shipped system (not just
# this build) so first-boot config silently takes defaults instead of blocking.
if command -v debconf-set-selections >/dev/null 2>&1; then
    echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections
    echo 'debconf debconf/priority select critical'       | debconf-set-selections
    dpkg-reconfigure -f noninteractive debconf >/dev/null 2>&1 || true
fi

# ---------------------------------------------------------------------------
log "app account: $APP_USER"
# ---------------------------------------------------------------------------
if ! id "$APP_USER" >/dev/null 2>&1; then
    useradd -m -s /bin/bash "$APP_USER"
    log "created user $APP_USER"
fi
# Stock Trixie Lite reserves uid 1000 as a `pi` PLACEHOLDER: a /usr/sbin/nologin
# shell, no home, and a primary gid with no matching group NAME. Creating-if-
# missing skips it, so three things break unless we repair them here:
#   * no login shell -> the per-device password can't log into console/SSH
#     (systemd services exec directly, so they run fine and mask it);
#   * no `pi` group  -> `Group=pi` in the kiosk/backend units fails with
#     "failed to determine group credentials" and the unit never starts;
#   * no home dir.
# Ensure the named group first (install -d -g and usermod -g both need it).
getent group "$APP_USER" >/dev/null 2>&1 || groupadd "$APP_USER"
usermod -g "$APP_USER" -s /bin/bash "$APP_USER"
install -d -o "$APP_USER" -g "$APP_USER" -m 755 "/home/$APP_USER"
# Groups the kiosk (video/render/audio, drm), Pisound (i2c/spi/gpio/audio), and
# input need, plus netdev for NM and sudo for maintenance. Missing groups on a
# given base are simply skipped.
for g in video render audio gpio i2c spi input netdev plugdev dialout sudo; do
    getent group "$g" >/dev/null 2>&1 && usermod -aG "$g" "$APP_USER" || true
done
# Lock the account's password; firstboot sets it to the per-device code so the
# printed password logs into the box (SSH/console) as well as the web UI + AP.
passwd -l "$APP_USER" >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
log "system packages: kiosk + networking"
# ---------------------------------------------------------------------------
# setup-pi.sh installs oF's own build deps + python3-venv/ffmpeg/avahi + Pisound.
# These are the extra runtime pieces the appliance needs: the X11 kiosk stack
# Pi OS Lite omits, nftables (captive redirect), NM + its shared-mode dnsmasq.
apt-get update
apt-get install -y --no-install-recommends \
    git ca-certificates \
    xinit xserver-xorg xserver-xorg-legacy x11-xserver-utils \
    nftables dnsmasq-base network-manager avahi-daemon avahi-utils \
    raspi-config e2fsprogs

# ---------------------------------------------------------------------------
if [[ "$PREBUILT" == "1" ]]; then
    log "app build: SKIPPED (LIVEPI_PREBUILT=1 -- binary + venv already in place)"
else
    log "app build: openFrameworks + renderer (this is the slow step under qemu)"
    OF_ROOT="$OF_ROOT" bash "$APP_DIR/scripts/setup-pi.sh"
    make -C "$APP_DIR" OF_ROOT="$OF_ROOT" -j"$(nproc)"
fi
# The backend venv is arch-specific and built here (setup-pi.sh already does it;
# this covers the prebuilt path and any partial state).
if [[ ! -x "$APP_DIR/backend/.venv/bin/uvicorn" ]]; then
    log "backend venv"
    python3 -m venv "$APP_DIR/backend/.venv"
    "$APP_DIR/backend/.venv/bin/pip" install -q -r "$APP_DIR/backend/requirements.txt"
fi
# The renderer binary and the built web UI must exist or the box boots to nothing.
[[ -x "$APP_DIR/bin/livepi-video-glitch" ]] || warn "renderer binary missing at $APP_DIR/bin/livepi-video-glitch"
[[ -f "$APP_DIR/frontend/dist/index.html" ]] || warn "frontend/dist missing -- build it on the host (npm run build) before provisioning; the web UI will not serve"

# ---------------------------------------------------------------------------
log "X11 kiosk config (Xwrapper)"
# ---------------------------------------------------------------------------
# Pi OS Lite's default allowed_users=console refuses to let the systemd kiosk
# service (no seat/VT owner) start X. See docs/deploy.md "Autostart at boot".
install -D -m 644 /dev/stdin /etc/X11/Xwrapper.config <<'XWRAP'
# Managed by LivePi (scripts/provision-appliance.sh). Lets the headless kiosk
# service start X; regenerated by `dpkg-reconfigure xserver-xorg-legacy`.
allowed_users=anybody
needs_root_rights=yes
XWRAP

# Appliance render config. The shipped bin/data/config/app.json defaults are
# DEV-oriented -- a 1280x720 WINDOWED render and the `mock` control source --
# which on a real box means a 1280x720 image in the corner of the panel (black
# everywhere else) and no response to the Pisound MIDI. This local override
# (merged over app.json at startup, per main.cpp) makes it an appliance:
# full-screen on whatever panel is attached, driven by the real Pisound control
# source (MIDI + button). Left out of the dev tree so desktop dev stays windowed.
install -d "$APP_DIR/bin/data/config"
cat > "$APP_DIR/bin/data/config/app.local.json" <<'APPCFG'
{
    "control_source": "pisound",
    "window": { "fullscreen": true }
}
APPCFG

# ---------------------------------------------------------------------------
log "systemd units"
# ---------------------------------------------------------------------------
# Render a unit template's placeholders and drop it in /etc/systemd/system.
# (In a chroot we can't `daemon-reload`, and don't need to -- the manager reads
#  these fresh at boot.)
render_unit() {
    local template="$1" dest="$2"; shift 2
    local args=() kv
    for kv in "$@"; do args+=(-e "s#${kv%%=*}#${kv#*=}#g"); done
    sed "${args[@]}" "$APP_DIR/systemd/$template" > "$dest"
}

render_unit livepi-dataprep.service.template   /etc/systemd/system/livepi-dataprep.service \
    __APP_DIR__="$APP_DIR" __DATA_DIR__="$DATA_DIR"
render_unit livepi-firstboot.service.template  /etc/systemd/system/livepi-firstboot.service \
    __APP_DIR__="$APP_DIR" __APP_USER__="$APP_USER" __DATA_DIR__="$DATA_DIR"
render_unit livepi-lockdown.service.template   /etc/systemd/system/livepi-lockdown.service \
    __APP_DIR__="$APP_DIR" __DATA_DIR__="$DATA_DIR" __LOCKDOWN__="$LOCKDOWN"
render_unit livepi-backend.service.template    /etc/systemd/system/livepi-backend.service \
    __APP_DIR__="$APP_DIR" __USER__="$APP_USER" __DATA_DIR__="$DATA_DIR"
render_unit livepi-video-glitch.service.template /etc/systemd/system/livepi-video-glitch.service \
    __APP_DIR__="$APP_DIR" __PI_USER__="$APP_USER" __DATA_DIR__="$DATA_DIR"

# Captive portal: the service unit is static; copy it + its rule files.
install -D -m 644 "$APP_DIR/systemd/livepi-captive.service" /etc/systemd/system/livepi-captive.service
install -D -m 644 "$APP_DIR/config/livepi-captive.nft" /etc/livepi/captive.nft
install -D -m 644 "$APP_DIR/config/networkmanager/dnsmasq-shared.d/livepi-captive.conf" \
    /etc/NetworkManager/dnsmasq-shared.d/livepi-captive.conf

# polkit: let the backend's app user drive NetworkManager headless.
render_unit ../polkit/50-livepi-network.rules.template /etc/polkit-1/rules.d/50-livepi-network.rules \
    __APP_USER__="$APP_USER"

# journald to RAM (no SD wear / power-cut corruption on the read-only box).
install -D -m 644 "$APP_DIR/config/journald-volatile.conf" \
    /etc/systemd/journald.conf.d/livepi-volatile.conf

# ---------------------------------------------------------------------------
log "enable services"
# ---------------------------------------------------------------------------
systemctl enable \
    livepi-dataprep.service \
    livepi-firstboot.service \
    livepi-lockdown.service \
    livepi-backend.service \
    livepi-video-glitch.service \
    livepi-captive.service
# Base services the appliance relies on.
for svc in NetworkManager.service avahi-daemon.service; do
    systemctl enable "$svc" 2>/dev/null || warn "could not enable $svc"
done
# SSH (headless reach). Trixie's openssh is SOCKET-activated: ssh.socket is what
# LISTENS on :22 and spawns ssh@.service per connection, so enabling ssh.service
# alone leaves nothing on the port (why the first sealed image had SSH "off").
# Enable whichever unit this base actually uses. Per-box host keys are
# (re)generated by firstboot.sh while the root is still writable, so they get
# baked into the read-only lower layer and stay stable across reboots.
raspi-config nonint do_ssh 0 2>/dev/null || true
if systemctl list-unit-files 2>/dev/null | grep -q '^ssh\.socket'; then
    systemctl disable ssh.service 2>/dev/null || true   # socket + service both binding :22 conflict
    systemctl enable ssh.socket 2>/dev/null || warn "could not enable ssh.socket"
else
    systemctl enable ssh.service 2>/dev/null || warn "could not enable ssh.service"
fi

# ---------------------------------------------------------------------------
log "WiFi regdomain: $WIFI_COUNTRY"
# ---------------------------------------------------------------------------
# The radio won't broadcast the AP without a country set. best-effort: the live
# `iw reg set` half is a no-op in a chroot, but the persistent config is written.
raspi-config nonint do_wifi_country "$WIFI_COUNTRY" 2>/dev/null || \
    warn "could not set WiFi country to $WIFI_COUNTRY (set it in the imager, or re-run on the Pi)"

# Pre-configure locale + keyboard so first boot is SILENT. Pi OS Lite ships
# these effectively unconfigured; on the first boot the keyboard-setup /
# console-setup services (and locales) otherwise throw a debconf "Package
# configuration" window on the console before the kiosk -- unacceptable on a
# sealed appliance. US defaults; adjust via the knobs if ever needed.
raspi-config nonint do_change_locale "${LIVEPI_LOCALE:-en_US.UTF-8}" 2>/dev/null || warn "could not set locale"

# Keyboard + console: configure DIRECTLY, not via raspi-config's nonint helper
# -- that left keyboard-configuration half-configured in the qemu chroot, so
# first boot ran its "Configuring keyboard-configuration" debconf dialog
# (confirmed on hardware). Write the defaults file, preseed the questions as
# SEEN, and reconfigure noninteractively so nothing is ever asked on the box.
KEYMAP="${LIVEPI_KEYMAP:-us}"
cat > /etc/default/keyboard <<KBD
XKBMODEL="pc105"
XKBLAYOUT="$KEYMAP"
XKBVARIANT=""
XKBOPTIONS=""
BACKSPACE="guess"
KBD
debconf-set-selections <<SEL
keyboard-configuration keyboard-configuration/modelcode string pc105
keyboard-configuration keyboard-configuration/layoutcode string $KEYMAP
keyboard-configuration keyboard-configuration/variantcode string
keyboard-configuration keyboard-configuration/optionscode string
keyboard-configuration keyboard-configuration/xkb-keymap select $KEYMAP
keyboard-configuration keyboard-configuration/store_defaults_in_debconf_db boolean true
console-setup console-setup/charmap47 select UTF-8
console-setup console-setup/codeset47 select . Combined - Latin; Slavic Cyrillic; Greek
SEL
dpkg-reconfigure -f noninteractive keyboard-configuration >/dev/null 2>&1 || warn "keyboard-configuration reconfigure failed"
dpkg-reconfigure -f noninteractive console-setup >/dev/null 2>&1 || warn "console-setup reconfigure failed"

# ---------------------------------------------------------------------------
log "disable Pi OS's stock rootfs auto-grow"
# ---------------------------------------------------------------------------
# CRUCIAL: stock Pi OS grows rootfs to fill the whole card on first boot. We
# need that free space to stay unclaimed so livepi-dataprep can make it the
# LIVEPI_DATA partition. Kill both the resize service and the cmdline init hook
# that drives Pi OS's first-run (which also does the resize); we regenerate the
# machine-id + SSH keys ourselves below, which is the hook's other job.
systemctl disable resize2fs_once.service 2>/dev/null || true
systemctl mask    resize2fs_once.service 2>/dev/null || true
CMDLINE="/boot/firmware/cmdline.txt"; [[ -f "$CMDLINE" ]] || CMDLINE="/boot/cmdline.txt"
if [[ -f "$CMDLINE" ]]; then
    # Strip only the resize/first-run init= token; leave every other param.
    sed -i -E 's# init=/usr/lib/raspberrypi-sys-mods/firstboot(\s+splash)?##; s# init=/usr/lib/raspi-config/init_resize\.sh##' "$CMDLINE"
    log "cmdline.txt: removed stock first-run/resize init hook"
else
    warn "no cmdline.txt found; ensure stock rootfs auto-grow is disabled some other way"
fi

# ---------------------------------------------------------------------------
log "data mountpoint + finalize"
# ---------------------------------------------------------------------------
install -d -m 755 "$DATA_DIR"

# Golden-master hygiene: strip anything that must be unique per box.
: > /etc/machine-id                       # systemd regenerates on first boot
rm -f /var/lib/dbus/machine-id
rm -f /etc/ssh/ssh_host_*                 # regenerate_ssh_host_keys makes new ones
rm -f /root/.bash_history "/home/$APP_USER/.bash_history" 2>/dev/null || true
find /var/log -type f -delete 2>/dev/null || true

# App tree owned by the app user (world-readable; read-only at runtime anyway).
chown -R "$APP_USER:$APP_USER" "$APP_DIR"

# Reclaim the openFrameworks build tree: the renderer statically links oF, so it
# is not needed at runtime, and the read-only box never recompiles (updates ship
# a prebuilt binary). Keep it only for a writable dev image.
if [[ "$KEEP_OF" != "1" && "$PREBUILT" != "1" && -d "$OF_ROOT" ]]; then
    rm -rf "$OF_ROOT"
    log "removed oF build tree $OF_ROOT (static-linked; not needed at runtime)"
fi

# Settle any package whose postinst was deferred/incomplete in the chroot, so
# first boot never runs an interactive `dpkg --configure` (the source of the
# debconf popup). Noninteractive frontend is already exported above.
dpkg --configure -a || warn "dpkg --configure -a reported problems (check the build log)"

apt-get clean
rm -rf /var/lib/apt/lists/*

log "provisioning complete"
echo "[provision] app=$APP_DIR user=$APP_USER data=$DATA_DIR lockdown=$LOCKDOWN"
echo "[provision] first boot will: dataprep -> firstboot -> services -> lockdown -> reboot"
