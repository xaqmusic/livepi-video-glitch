#!/usr/bin/env bash
# First-boot personalization for a shipped LivePi box. Runs ONCE, as root,
# from livepi-firstboot.service BEFORE the backend and kiosk units start
# (see docs/distribution.md "First boot & device identity").
#
# What it does, all idempotent and guarded by a marker on $DATA_DIR so a
# reboot or a re-run never re-rolls a box that's already personalized:
#   1. Per-device secrets  -- a random LIVEPI_PASSWORD and LIVEPI_SECRET_KEY
#      written into $DATA_DIR/backend/.env, replacing the shared insecure
#      dev defaults ("livepi" / "livepi-dev-secret-change-me") that every
#      build otherwise ships with. THE security must-fix before any image
#      goes out (docs/distribution.md "Security posture").
#   2. Data layout        -- seeds the $DATA_DIR tree the backend expects
#      (it self-seeds a default show into it) and hands it to the app user.
#   3. Device identity     -- a unique hostname livepi-XXXX derived from the
#      Pi's own serial so multiple boxes on one network don't collide, plus
#      avahi so the box answers at <hostname>.local.
#
# The secret generation is deliberately dependency-light (only /dev/urandom
# and coreutils -- no openssl/xxd, which Pi OS Lite doesn't guarantee) and
# the whole thing is parameterized on LIVEPI_DATA_DIR so it can be dry-run
# off-Pi against a throwaway directory:
#
#   LIVEPI_DATA_DIR=/tmp/livepi-data LIVEPI_PROVISION_SYSTEM=0 \
#       ./scripts/firstboot.sh
#
# LIVEPI_PROVISION_SYSTEM=0 skips the system-level bits (hostname, avahi,
# chown) so the secret/seed logic can be exercised without root or a real Pi.
set -euo pipefail

DATA_DIR="${LIVEPI_DATA_DIR:-/data}"
APP_USER="${LIVEPI_APP_USER:-pi}"
PROVISION_SYSTEM="${LIVEPI_PROVISION_SYSTEM:-1}"

MARKER="$DATA_DIR/.provisioned"
ENV_FILE="$DATA_DIR/backend/.env"
# Must match AP_CON_NAME in backend/livepi_backend/network.py.
AP_CON_NAME="livepi-ap"

log() { printf '[firstboot] %s\n' "$*"; }

# --- idempotency guard -------------------------------------------------
# The marker is the single source of truth for "this box is personalized."
# Present => do nothing (a reboot re-runs the oneshot; it must be a no-op).
if [[ -f "$MARKER" ]]; then
    log "already provisioned ($MARKER exists) -- nothing to do"
    exit 0
fi

log "provisioning a fresh box (DATA_DIR=$DATA_DIR)"

# --- randomness --------------------------------------------------------
# Filter /dev/urandom to a charset. head closing the pipe kills tr with
# SIGPIPE (141), which pipefail would surface as a failure -- swallow it;
# head has already emitted the bytes we want by then.
rand_from() {  # <charset> <count>
    LC_ALL=C tr -dc "$1" < /dev/urandom 2>/dev/null | head -c "$2" || true
}

# Human-typeable factory password: 12 chars from an unambiguous set (no
# 0/O/1/l/i to misread off a screen or a card), grouped xxxx-xxxx-xxxx.
gen_password() {
    local raw
    raw="$(rand_from 'abcdefghjkmnpqrstuvwxyz23456789' 12)"
    printf '%s-%s-%s' "${raw:0:4}" "${raw:4:4}" "${raw:8:4}"
}

# --- per-device secrets into $DATA_DIR/backend/.env --------------------
# Loaded by the backend systemd unit's EnvironmentFile before the process
# starts (auth.py binds SECRET_KEY into a TimestampSigner AT IMPORT TIME,
# so the value has to be in the environment first). We fill only missing
# keys so a hand-edited .env or a partial previous run is respected.
mkdir -p "$(dirname "$ENV_FILE")"
touch "$ENV_FILE"
chmod 600 "$ENV_FILE"

has_env() { grep -qE "^$1=" "$ENV_FILE"; }
get_env() { grep -E "^$1=" "$ENV_FILE" | tail -n1 | cut -d= -f2- || true; }

if ! has_env LIVEPI_SECRET_KEY; then
    echo "LIVEPI_SECRET_KEY=$(rand_from '0-9a-f' 64)" >> "$ENV_FILE"
    log "generated LIVEPI_SECRET_KEY (256-bit)"
fi
if ! has_env LIVEPI_PASSWORD; then
    echo "LIVEPI_PASSWORD=$(gen_password)" >> "$ENV_FILE"
    log "generated LIVEPI_PASSWORD"
fi
if ! has_env LIVEPI_DATA_DIR; then
    echo "LIVEPI_DATA_DIR=$DATA_DIR" >> "$ENV_FILE"
fi

INITIAL_PW="$(get_env LIVEPI_PASSWORD)"

# --- seed the data tree ------------------------------------------------
# The backend self-seeds a default show into shows/, and the ingest/thumb
# pipeline writes under clips/ -- just make the dirs exist and belong to
# the app user (firstboot runs as root; the backend does not).
for d in shows clips clips/.thumbs clips/.pingpong config backend; do
    mkdir -p "$DATA_DIR/$d"
done

if [[ "$PROVISION_SYSTEM" == "1" ]]; then
    if id "$APP_USER" >/dev/null 2>&1; then
        chown -R "$APP_USER:$APP_USER" "$DATA_DIR"
        chmod 600 "$ENV_FILE"   # chown -R reset the mode; re-tighten
        log "handed $DATA_DIR to $APP_USER"
        # Unify the box's one secret: the printed password also logs the app
        # user into the console + SSH. The image ships the account
        # password-LOCKED (scripts/provision-appliance.sh runs `passwd -l`, so
        # there's never a shipped default login); this is where it gets the
        # per-device code, matching the web-UI login and the AP's WPA2 key.
        printf '%s:%s\n' "$APP_USER" "$INITIAL_PW" | chpasswd
        log "set $APP_USER console/SSH password to the per-device code"
    else
        log "WARNING: app user '$APP_USER' does not exist; left $DATA_DIR root-owned"
    fi
fi

# --- device identity: unique hostname + mDNS --------------------------
# Suffix from the Pi's own serial (stable across reboots for a given box),
# so re-imaging the same hardware yields the same name and two boxes on one
# LAN never collide. Fall back to random if the serial can't be read.
derive_suffix() {
    local serial=""
    if [[ -r /sys/firmware/devicetree/base/serial-number ]]; then
        serial="$(tr -d '\0' < /sys/firmware/devicetree/base/serial-number 2>/dev/null || true)"
    fi
    [[ -n "$serial" ]] || serial="$(awk '/^Serial/ {print $3}' /proc/cpuinfo 2>/dev/null || true)"
    local suffix
    suffix="$(printf '%s' "$serial" | LC_ALL=C tr 'A-Z' 'a-z' | tr -dc 'a-z0-9')"
    suffix="${suffix: -4}"
    [[ -n "$suffix" ]] || suffix="$(rand_from 'a-z0-9' 4)"
    printf '%s' "$suffix"
}

SUFFIX="$(derive_suffix)"
NEW_HOSTNAME="livepi-$SUFFIX"   # OS hostname / SSH / mDNS
AP_SSID="LivePi-$SUFFIX"        # broadcast name of the control-network AP

if [[ "$PROVISION_SYSTEM" == "1" ]]; then
    current="$(hostnamectl --static 2>/dev/null || cat /etc/hostname 2>/dev/null || true)"
    if [[ "$current" != "$NEW_HOSTNAME" ]]; then
        hostnamectl set-hostname "$NEW_HOSTNAME"
        # Keep the loopback alias in sync so `sudo` etc. resolve the name.
        if grep -qE '^127\.0\.1\.1' /etc/hosts 2>/dev/null; then
            sed -i -E "s/^127\.0\.1\.1.*/127.0.1.1\t$NEW_HOSTNAME/" /etc/hosts
        else
            printf '127.0.1.1\t%s\n' "$NEW_HOSTNAME" >> /etc/hosts
        fi
        log "hostname set to $NEW_HOSTNAME"
    fi
    # avahi publishes <hostname>.local; a shared http://livepi.local alias
    # is added by livepi-mdns-alias.service (see scripts/install-mdns-alias.sh).
    systemctl enable --now avahi-daemon >/dev/null 2>&1 || \
        log "WARNING: could not enable avahi-daemon (is it installed?)"

    # SSH host keys: the image ships with none (so every box is unique). Generate
    # them here, on the first boot while the root is still writable (before
    # lockdown), so they bake into the read-only layer and stay stable across
    # reboots. ssh-keygen -A is a no-op once they exist.
    ssh-keygen -A >/dev/null 2>&1 || log "WARNING: ssh-keygen -A failed"

    # --- standing control-network AP ----------------------------------
    # The box's own hotspot. autoconnect at the LOWEST priority makes it the
    # automatic fallback: any venue-WiFi client profile (default priority 0)
    # outranks it, so NM keeps venue WiFi when it's in range and drops back
    # to the AP when it isn't -- the whole "autohotspot" mode-switch, native
    # to NM (verified on the Pi 4's brcmfmac; no dispatcher script needed).
    # WPA2 key = the printed login password, so the box has one secret to
    # share (join the hotspot and log in with the same code). ipv4 shared
    # gives clients DHCP + NAT out any uplink.
    # NB: the WiFi radio needs an unblocked country/regdomain to actually
    # broadcast -- set by the imager/image build; firstboot only unblocks it.
    if command -v nmcli >/dev/null 2>&1; then
        nmcli radio wifi on >/dev/null 2>&1 || true
        nmcli connection delete "$AP_CON_NAME" >/dev/null 2>&1 || true
        if nmcli connection add type wifi ifname wlan0 con-name "$AP_CON_NAME" \
                autoconnect yes connection.autoconnect-priority -999 \
                ssid "$AP_SSID" 802-11-wireless.mode ap 802-11-wireless.band bg \
                ipv4.method shared \
                wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$INITIAL_PW" >/dev/null 2>&1; then
            log "created control AP '$AP_SSID' (WPA2; key = the printed password)"
        else
            log "WARNING: could not create the control AP profile"
        fi
    fi
else
    log "PROVISION_SYSTEM=0 -- skipping hostname/avahi/AP (would be $NEW_HOSTNAME / $AP_SSID)"
fi

# --- mark done ---------------------------------------------------------
printf 'provisioned %s\nhostname %s\nap %s\n' \
    "$(date -u +%FT%TZ)" "$NEW_HOSTNAME" "$AP_SSID" > "$MARKER"

log "done. hostname=$NEW_HOSTNAME  AP=$AP_SSID  initial password=$INITIAL_PW"
log "reach the box at http://$NEW_HOSTNAME.local (or join the '$AP_SSID' hotspot)"
