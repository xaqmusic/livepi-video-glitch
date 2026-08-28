#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
# LivePi in-app updater -- activation engine. The read-only-root appliance can
# never modify /opt/livepi (the factory app, baked into the RO image), so an
# "update" is a self-contained app tree dropped on the WRITABLE /data partition
# and made live by flipping ONE atomic symlink. The systemd units for the
# renderer + backend point at /data/app/current, so swapping that symlink and
# restarting the two services is the whole activation -- no reflash, ever.
#
#   /opt/livepi                     factory app (immutable = permanent rollback anchor)
#   /data/app/versions/<v>/         installed update bundles (each a full app tree)
#   /data/app/current   -> ...      the ACTIVE tree (units reference this path)
#   /data/app/last-good -> ...      the last tree that came up healthy
#   /data/app/incoming/pending.*    an uploaded bundle awaiting apply
#   /data/app/state/                pending marker, boot counter, last-apply.json
#   /data/app/logs/apply.log        rolling apply log (journal is volatile)
#
# THIS script always runs from /opt/livepi (the factory copy) -- it is the
# rollback machinery, so it must not itself live on the swappable tree. It is
# invoked three ways:
#   * livepi-app-activate.service (boot, before the app)  -> `boot`
#   * livepi-app-confirm.service  (boot, after the app)   -> `confirm`
#   * the backend, via a tight sudoers line               -> `apply` / `rollback`
#
# Safety model -- an update can never brick the box:
#   * `apply` health-gates the new version and auto-rolls-back within ~30s if the
#     backend or renderer don't come up. No reboot needed for the common failure.
#   * if power is lost mid-apply (swap done, not yet confirmed), the `pending`
#     marker survives; `boot` gives the trial version a couple of boots to prove
#     healthy (via `confirm`) and rolls back to last-good if it boot-loops.
#   * a dangling `current` (e.g. a deleted version dir) self-heals to last-good,
#     and last-good self-heals to the factory. There is always a bootable tree.
set -euo pipefail

FACTORY="${LIVEPI_FACTORY_DIR:-/opt/livepi}"
DATA_DIR="${LIVEPI_DATA_DIR:-/data}"
APP_ROOT="$DATA_DIR/app"
VERSIONS="$APP_ROOT/versions"
INCOMING="$APP_ROOT/incoming"
STATE="$APP_ROOT/state"
LOGDIR="$APP_ROOT/logs"
CURRENT="$APP_ROOT/current"
LASTGOOD="$APP_ROOT/last-good"
PENDING="$STATE/pending"
BOOTCOUNT="$STATE/boot-attempts"
APPLYLOG="$LOGDIR/apply.log"
RESULT="$STATE/last-apply.json"

# A trial version that has not confirmed healthy after this many boots is
# abandoned for last-good. 2 => one power-loss-mid-apply reboot is tolerated,
# a genuine boot-loop is caught on the second.
BOOT_LIMIT="${LIVEPI_BOOT_LIMIT:-2}"
# How long `apply`/`confirm` wait for the backend + renderer to come up healthy.
HEALTH_TIMEOUT="${LIVEPI_HEALTH_TIMEOUT:-30}"

# Renderer heartbeat + backend health endpoint the gate polls.
STATUS_PATH="${LIVEPI_STATUS_PATH:-/tmp/livepi/status.json}"
HEALTH_URL="${LIVEPI_HEALTH_URL:-http://127.0.0.1:8080/api/health}"

SERVICES=(livepi-backend.service livepi-video-glitch.service)

log() {
    local msg="[app-activate] $*"
    printf '%s\n' "$msg"
    # Best-effort persistent log (journal is volatile on the appliance).
    mkdir -p "$LOGDIR" 2>/dev/null && printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo -)" "$msg" >> "$APPLYLOG" 2>/dev/null || true
}

ensure_layout() {
    mkdir -p "$VERSIONS" "$INCOMING" "$STATE" "$LOGDIR"
    # The backend (app user) writes the uploaded bundle into incoming/; the rest
    # stays root-owned (world-readable state the backend only reads). LIVEPI_APP_USER
    # is set by the boot unit; absent on the sudo apply/rollback calls, where
    # incoming already exists from boot, so the skip is harmless.
    [[ -n "${LIVEPI_APP_USER:-}" ]] && chown "$LIVEPI_APP_USER" "$INCOMING" 2>/dev/null || true
}

# Absolute path a name-symlink resolves to, or empty if missing/dangling.
resolve() {
    local link="$1"
    [[ -L "$link" || -e "$link" ]] || { echo ""; return; }
    local tgt
    tgt="$(readlink -f "$link" 2>/dev/null || true)"
    # A resolved tree must actually contain the renderer binary to count as valid
    # (guards against a half-extracted or pruned version dir).
    [[ -n "$tgt" && -x "$tgt/bin/livepi-video-glitch" ]] && echo "$tgt" || echo ""
}

# Point a name-symlink at a target atomically (write-new + rename over).
point() {
    local link="$1" target="$2"
    ln -sfn "$target" "$link.tmp"
    mv -Tf "$link.tmp" "$link"
}

# Read the "version" field from a tree's manifest.json (best-effort; falls back
# to the directory name). No jq on the appliance -> a small python one-liner,
# which the backend guarantees is present.
tree_version() {
    local tree="$1"
    if [[ -f "$tree/manifest.json" ]]; then
        python3 - "$tree/manifest.json" <<'PY' 2>/dev/null && return 0 || true
import json,sys
try:
    print(json.load(open(sys.argv[1])).get("version","") or "")
except Exception:
    print("")
PY
    fi
    basename "$tree"
}

write_result() {
    # $1=status $2=message ; records the running version so the UI can render it.
    local status="$1" message="$2" ver=""
    ver="$(tree_version "$(resolve "$CURRENT")" 2>/dev/null || true)"
    mkdir -p "$STATE" 2>/dev/null || true
    python3 - "$RESULT" "$status" "$message" "$ver" <<'PY' 2>/dev/null || true
import json,sys
path,status,message,ver=sys.argv[1:5]
json.dump({"status":status,"message":message,"version":ver}, open(path,"w"))
PY
}

# ------------------------------------------------------------------- boot ----
# Runs before the app. Guarantees `current` points at a bootable tree and
# handles the boot-loop rollback for an unconfirmed trial version.
cmd_boot() {
    ensure_layout

    # Heal last-good first so it's a valid fallback, then current.
    [[ -n "$(resolve "$LASTGOOD")" ]] || point "$LASTGOOD" "$FACTORY"
    if [[ -z "$(resolve "$CURRENT")" ]]; then
        local fallback; fallback="$(resolve "$LASTGOOD")"; fallback="${fallback:-$FACTORY}"
        log "current missing/dangling -> healing to $fallback"
        point "$CURRENT" "$fallback"
    fi

    # Boot-loop guard for an un-confirmed trial version.
    if [[ -f "$PENDING" ]]; then
        local pend cur_ver n
        pend="$(cat "$PENDING" 2>/dev/null || true)"
        cur_ver="$(tree_version "$(resolve "$CURRENT")")"
        if [[ "$cur_ver" != "$pend" ]]; then
            # current no longer the trial version (already rolled back elsewhere).
            rm -f "$PENDING" "$BOOTCOUNT"
        else
            n=$(( $(cat "$BOOTCOUNT" 2>/dev/null || echo 0) + 1 ))
            if (( n >= BOOT_LIMIT )); then
                local good; good="$(resolve "$LASTGOOD")"; good="${good:-$FACTORY}"
                log "trial version '$pend' did not confirm in $n boots -> rolling back to $(tree_version "$good")"
                point "$CURRENT" "$good"
                rm -f "$PENDING" "$BOOTCOUNT"
                write_result "rolled-back" "Update '$pend' failed to start and was rolled back."
            else
                echo "$n" > "$BOOTCOUNT"
                log "trial version '$pend' unconfirmed, boot attempt $n/$BOOT_LIMIT"
            fi
        fi
    fi
    log "boot: current=$(tree_version "$(resolve "$CURRENT")") last-good=$(tree_version "$(resolve "$LASTGOOD")")"
}

# --------------------------------------------------------------- health ------
# True once BOTH the backend answers /api/health AND the renderer heartbeat is
# fresh (written within the last 15s). Waits up to HEALTH_TIMEOUT.
wait_healthy() {
    local deadline=$(( $(date +%s) + HEALTH_TIMEOUT ))
    while (( $(date +%s) < deadline )); do
        if curl -fsS --max-time 3 "$HEALTH_URL" >/dev/null 2>&1 && renderer_fresh; then
            return 0
        fi
        sleep 2
    done
    return 1
}

# The renderer rewrites status.json every frame; treat a mtime within 15s as
# "rendering". (No jq needed -- freshness of the heartbeat file is enough.)
renderer_fresh() {
    [[ -f "$STATUS_PATH" ]] || return 1
    local age=$(( $(date +%s) - $(stat -c %Y "$STATUS_PATH" 2>/dev/null || echo 0) ))
    (( age >= 0 && age <= 15 ))
}

restart_app() {
    systemctl restart "${SERVICES[@]}"
}

# --------------------------------------------------------------- confirm -----
# Runs after the app at boot. If a trial version is now healthy, promote it to
# last-good and clear the pending marker so the boot guard stops counting.
cmd_confirm() {
    ensure_layout
    [[ -f "$PENDING" ]] || { log "confirm: nothing pending"; exit 0; }
    local pend cur_ver
    pend="$(cat "$PENDING" 2>/dev/null || true)"
    cur_ver="$(tree_version "$(resolve "$CURRENT")")"
    if [[ "$cur_ver" != "$pend" ]]; then
        rm -f "$PENDING" "$BOOTCOUNT"
        exit 0
    fi
    if wait_healthy; then
        point "$LASTGOOD" "$(resolve "$CURRENT")"
        rm -f "$PENDING" "$BOOTCOUNT"
        log "confirm: '$pend' healthy -> promoted to last-good"
        write_result "ok" "Update '$pend' is active."
    else
        # Leave pending set: the next boot's guard will count this failure and
        # roll back once BOOT_LIMIT is reached. (Don't roll back here -- a slow
        # first frame shouldn't discard an otherwise-good version on one boot.)
        log "confirm: '$pend' not healthy within ${HEALTH_TIMEOUT}s -- deferring to boot guard"
    fi
}

# ----------------------------------------------------------------- apply -----
# Entry point the backend calls (via sudo). Does the fast, synchronous checks,
# then hands the swap+restart+gate to a detached transient unit so that
# restarting the backend mid-apply cannot kill the job. The UI polls status.
cmd_apply() {
    ensure_layout
    local bundle="$INCOMING/pending.tar.zst"
    if [[ ! -f "$bundle" ]]; then
        write_result "failed" "No uploaded bundle found."
        echo "no bundle at $bundle" >&2; exit 1
    fi
    write_result "verifying" "Checking the uploaded bundle..."
    # Detach the heavy work: systemd-run puts it in its own transient scope so it
    # survives the livepi-backend restart this apply performs. --collect GCs the
    # unit on exit; a fixed name rejects a second concurrent apply.
    if systemctl is-active --quiet livepi-apply.service 2>/dev/null; then
        write_result "failed" "An update is already in progress."
        echo "apply already running" >&2; exit 1
    fi
    systemd-run --collect --quiet --unit=livepi-apply \
        --setenv=LIVEPI_FACTORY_DIR="$FACTORY" --setenv=LIVEPI_DATA_DIR="$DATA_DIR" \
        "$FACTORY/scripts/app-activate.sh" _worker
    echo "apply started"
}

# The detached worker: verify -> extract -> atomic swap -> restart -> health
# gate -> promote or roll back. Writes progress to last-apply.json throughout.
cmd_worker() {
    ensure_layout
    local bundle="$INCOMING/pending.tar.zst"
    local sha="$INCOMING/pending.sha256"

    write_result "verifying" "Verifying the update bundle..."
    if [[ -f "$sha" ]]; then
        # The .sha256 holds the expected digest (bare hex or `hash  name`).
        local want got
        want="$(awk '{print $1; exit}' "$sha")"
        got="$(sha256sum "$bundle" | awk '{print $1}')"
        if [[ "$want" != "$got" ]]; then
            log "worker: checksum mismatch (want $want got $got)"
            write_result "failed" "Bundle checksum did not match -- upload may be corrupt."
            rm -f "$bundle" "$sha"; exit 1
        fi
    fi

    # Peek the version out of the bundle's manifest WITHOUT extracting the tree.
    local ver stage
    ver="$(tar --use-compress-program=unzstd -xOf "$bundle" ./manifest.json 2>/dev/null \
            | python3 -c 'import json,sys; print(json.load(sys.stdin).get("version",""))' 2>/dev/null || true)"
    if [[ -z "$ver" ]]; then
        write_result "failed" "Bundle has no version manifest -- not a LivePi update."
        rm -f "$bundle" "$sha"; exit 1
    fi
    stage="$VERSIONS/$ver"

    write_result "installing" "Installing version $ver..."
    rm -rf "$stage.partial"
    mkdir -p "$stage.partial"
    if ! tar --use-compress-program=unzstd -xf "$bundle" -C "$stage.partial"; then
        write_result "failed" "Could not unpack the update bundle."
        rm -rf "$stage.partial"; rm -f "$bundle" "$sha"; exit 1
    fi
    if [[ ! -x "$stage.partial/bin/livepi-video-glitch" ]]; then
        write_result "failed" "Bundle is missing the renderer binary."
        rm -rf "$stage.partial"; rm -f "$bundle" "$sha"; exit 1
    fi
    rm -rf "$stage"
    mv -T "$stage.partial" "$stage"
    rm -f "$bundle" "$sha"

    # Remember where to roll back to, then swap + mark the trial as pending
    # BEFORE restarting (so a power loss during restart is caught by the boot
    # guard). last-good stays at the currently-proven tree until this confirms.
    local prev; prev="$(resolve "$CURRENT")"; prev="${prev:-$FACTORY}"
    echo "$ver" > "$PENDING"; echo 0 > "$BOOTCOUNT"
    point "$CURRENT" "$stage"
    log "worker: swapped current -> $ver (was $(tree_version "$prev")), restarting app"

    write_result "restarting" "Starting version $ver..."
    if ! restart_app; then
        log "worker: restart failed -> rolling back to $(tree_version "$prev")"
        point "$CURRENT" "$prev"; rm -f "$PENDING" "$BOOTCOUNT"; restart_app || true
        write_result "rolled-back" "Version $ver failed to start; rolled back."
        exit 1
    fi

    write_result "verifying" "Checking version $ver is healthy..."
    if wait_healthy; then
        point "$LASTGOOD" "$stage"
        rm -f "$PENDING" "$BOOTCOUNT"
        log "worker: $ver healthy -> promoted to last-good"
        write_result "ok" "Updated to version $ver."
    else
        log "worker: $ver unhealthy within ${HEALTH_TIMEOUT}s -> rolling back to $(tree_version "$prev")"
        point "$CURRENT" "$prev"; rm -f "$PENDING" "$BOOTCOUNT"; restart_app || true
        write_result "rolled-back" "Version $ver did not come up healthy; rolled back."
        exit 1
    fi
}

# -------------------------------------------------------------- rollback -----
# Manual revert to last-good (or factory). Also detached, same reasoning.
cmd_rollback() {
    ensure_layout
    local good; good="$(resolve "$LASTGOOD")"; good="${good:-$FACTORY}"
    local cur; cur="$(resolve "$CURRENT")"
    if [[ "$good" == "$cur" ]]; then
        # last-good IS current -> fall all the way back to factory.
        good="$FACTORY"
    fi
    if systemctl is-active --quiet livepi-apply.service 2>/dev/null; then
        echo "an update is in progress" >&2; exit 1
    fi
    systemd-run --collect --quiet --unit=livepi-apply \
        --setenv=LIVEPI_FACTORY_DIR="$FACTORY" --setenv=LIVEPI_DATA_DIR="$DATA_DIR" \
        --setenv=LIVEPI_ROLLBACK_TO="$good" \
        "$FACTORY/scripts/app-activate.sh" _rollback_worker
    echo "rollback started"
}

cmd_rollback_worker() {
    ensure_layout
    local good="${LIVEPI_ROLLBACK_TO:-$FACTORY}"
    write_result "restarting" "Rolling back to $(tree_version "$good")..."
    point "$CURRENT" "$good"
    rm -f "$PENDING" "$BOOTCOUNT"
    restart_app || true
    if wait_healthy; then
        point "$LASTGOOD" "$good"
        write_result "ok" "Rolled back to $(tree_version "$good")."
    else
        write_result "rolled-back" "Rolled back to $(tree_version "$good") (health check pending)."
    fi
}

# ---------------------------------------------------------------- status -----
# Print a JSON snapshot (the backend serves this straight through). Read-only,
# no root needed -- state is world-readable.
cmd_status() {
    ensure_layout 2>/dev/null || true
    python3 - "$FACTORY" "$(resolve "$CURRENT")" "$(resolve "$LASTGOOD")" \
        "$(cat "$PENDING" 2>/dev/null || true)" "$RESULT" <<'PY'
import json,sys,os
def man(tree):
    if not tree: return None
    p=os.path.join(tree,"manifest.json")
    try:
        m=json.load(open(p)); return {"version":m.get("version"),"gitHash":m.get("gitHash"),"builtAt":m.get("builtAt"),"path":tree}
    except Exception:
        return {"version":os.path.basename(tree),"path":tree}
factory,current,lastgood,pending,result=sys.argv[1:6]
last=None
try: last=json.load(open(result))
except Exception: pass
print(json.dumps({
    "factory":man(factory),"current":man(current),"lastGood":man(lastgood),
    "pending":pending or None,"lastApply":last,
}))
PY
}

case "${1:-}" in
    boot)             cmd_boot ;;
    confirm)          cmd_confirm ;;
    apply)            cmd_apply ;;
    _worker)          cmd_worker ;;
    rollback)         cmd_rollback ;;
    _rollback_worker) cmd_rollback_worker ;;
    status)           cmd_status ;;
    *) echo "usage: app-activate.sh {boot|confirm|apply|rollback|status}" >&2; exit 2 ;;
esac
