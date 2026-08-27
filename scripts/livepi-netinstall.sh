#!/usr/bin/env bash
# Read or set the Pi bootloader's NET_INSTALL_AT_POWER_ON flag -- and NOTHING
# else in the EEPROM.
#
# Why it exists: the Pi 5 firmware shows a pink/white network-install screen
# with a QR code at power-on. It is drawn by the BOOTLOADER, before the kernel,
# which is why config.txt's disable_splash and cmdline's logo.nologo have no
# effect on it. For an appliance that screen is the first thing a customer sees
# and it invites them to reinstall the OS, so an owner needs to be able to turn
# it off from the gear menu.
#
# Why it is a separate script with a tight sudoers line rather than the backend
# shelling out to rpi-eeprom-config: this writes the BOARD's EEPROM, not the SD
# card. It survives a reflash and a card swap. Handing the web UI general
# rpi-eeprom-config access would put every bootloader setting (BOOT_ORDER, the
# recovery paths) one bug away from a web request. This script accepts exactly
# three verbs, rewrites exactly one key, and refuses anything else.
#
#   livepi-netinstall.sh get       -> prints "on" or "off" (or "unsupported")
#   livepi-netinstall.sh on|off    -> stages the change; applies at next boot
set -euo pipefail

KEY=NET_INSTALL_AT_POWER_ON

command -v rpi-eeprom-config >/dev/null 2>&1 || { echo "unsupported"; exit 0; }

read_flag() {
    local cur
    cur="$(rpi-eeprom-config 2>/dev/null | grep -E "^${KEY}=" | head -1 | cut -d= -f2 || true)"
    # Absent means the firmware default, which IS the prompt on a Pi 5.
    case "${cur:-1}" in
        0) echo off ;;
        *) echo on ;;
    esac
}

case "${1:-}" in
    get)
        read_flag
        ;;
    on|off)
        # ALWAYS apply -- never short-circuit on "it already looks right".
        #
        # On a Pi 5 `--apply` commits straight into the A/B EEPROM's INACTIVE
        # slot ("Force commit opposite: SUCCESS") and takes effect at the next
        # boot. There is no pending-update state to inspect: `rpi-eeprom-config`
        # keeps reporting the ACTIVE slot and `rpi-eeprom-update` says "up to
        # date". So comparing against the current value is worthless here -- it
        # would report "unchanged" while the board was still set to boot into
        # the opposite state. The caller tracks intent; this just applies it.
        want=$([ "$1" = "on" ] && echo 1 || echo 0)
        tmp="$(mktemp)"
        trap 'rm -f "$tmp"' EXIT
        rpi-eeprom-config > "$tmp"
        if grep -qE "^${KEY}=" "$tmp"; then
            sed -i "s/^${KEY}=.*/${KEY}=${want}/" "$tmp"
        else
            printf '%s=%s\n' "$KEY" "$want" >> "$tmp"
        fi
        # Builds a new EEPROM image from the CURRENT one with this config; it
        # does not bump the bootloader version. Errors are NOT swallowed: an
        # earlier version sent output to /dev/null and cheerfully printed
        # "staged" after a failed write, which is exactly the sort of lie that
        # costs an afternoon.
        if rpi-eeprom-config --apply "$tmp" >/dev/null 2>&1; then
            echo "applied"
        else
            echo "failed" >&2
            exit 1
        fi
        ;;
    *)
        echo "usage: $0 get|on|off" >&2
        exit 2
        ;;
esac
