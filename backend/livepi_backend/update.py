# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
"""In-app updater API: report the installed / last-good / factory versions,
accept an uploaded bundle, and trigger apply or rollback. All the risky work --
verify, atomic symlink swap, restart, health-gate, auto-rollback -- lives in
scripts/app-activate.sh and runs as root via a tight sudoers line; this router
is a thin, session-gated front door that hands the bundle over and reports state.

On a dev box (no /data/app) every endpoint degrades to available:false rather
than erroring, so the gear menu can simply hide the panel."""

import json
import shutil
import subprocess
from pathlib import Path

from fastapi import APIRouter, Depends, HTTPException, UploadFile

from . import config
from .auth import require_session

router = APIRouter(dependencies=[Depends(require_session)])

_INCOMING = config.APP_ROOT / "incoming"
_STATE = config.APP_ROOT / "state"


def _manifest(tree: Path | None) -> dict | None:
    if not tree:
        return None
    try:
        m = json.loads((tree / "manifest.json").read_text())
        return {
            "version": m.get("version"),
            "gitHash": m.get("gitHash"),
            "builtAt": m.get("builtAt"),
            "channel": m.get("channel"),
        }
    except Exception:
        # A tree with no manifest still has an identity: its directory name.
        return {"version": tree.name}


def _resolve(link: Path) -> Path | None:
    """The tree a name-symlink points at, or None if missing/dangling. A valid
    tree must actually contain the renderer binary (mirrors app-activate.sh)."""
    try:
        tgt = link.resolve()
        return tgt if (tgt / "bin" / "livepi-video-glitch").exists() else None
    except Exception:
        return None


@router.get("/api/update/status")
def update_status():
    if not config.APP_ROOT.is_dir():
        return {"available": False}
    pending = None
    try:
        pending = ((_STATE / "pending").read_text().strip() or None)
    except Exception:
        pass
    last_apply = None
    try:
        last_apply = json.loads((_STATE / "last-apply.json").read_text())
    except Exception:
        pass
    factory = config.FACTORY_DIR
    return {
        "available": True,
        "current": _manifest(_resolve(config.APP_ROOT / "current")),
        "lastGood": _manifest(_resolve(config.APP_ROOT / "last-good")),
        "factory": _manifest(factory if factory.exists() else None),
        "pending": pending,
        "lastApply": last_apply,
        # The original per-device code (LIVEPI_PASSWORD). Changing the web
        # password only rewrote auth.json -- this stays the box's unix/SSH/sudo
        # login AND the Wi-Fi hotspot key, so the owner needs it back to admin
        # the Pi. Safe to return here: the endpoint is session-gated, unlike the
        # on-screen connection card (public projector) where we hide it.
        "deviceCode": config.PASSWORD,
    }


def _run_activator(verb: str) -> str:
    # sudo -n so a missing sudoers line fails fast instead of hanging on a
    # password prompt. apply/rollback detach their heavy work (systemd-run) and
    # return in well under a second, so a short timeout is plenty.
    try:
        proc = subprocess.run(
            ["sudo", "-n", str(config.ACTIVATOR), verb],
            capture_output=True, text=True, timeout=30,
        )
    except FileNotFoundError:
        raise HTTPException(500, "updater is not installed on this device")
    except subprocess.TimeoutExpired:
        raise HTTPException(504, "the update engine did not respond")
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "update failed").strip()
        raise HTTPException(500, detail[:300])
    return (proc.stdout or "").strip()


@router.post("/api/update/upload")
def update_upload(file: UploadFile):
    if not config.APP_ROOT.is_dir():
        raise HTTPException(400, "this device has no updatable app partition")
    name = file.filename or ""
    if not name.endswith(".tar.zst"):
        raise HTTPException(400, "expected a .tar.zst LivePi update bundle")
    # incoming/ is created + handed to this account by the boot activator; write
    # to a .part then rename so a truncated upload can't be picked up as ready.
    _INCOMING.mkdir(parents=True, exist_ok=True)
    dest = _INCOMING / "pending.tar.zst"
    tmp = _INCOMING / "pending.tar.zst.part"
    with open(tmp, "wb") as out:
        shutil.copyfileobj(file.file, out, length=1024 * 1024)
    tmp.replace(dest)
    # No client checksum on the upload path; the bundle's own manifest + tar
    # integrity gate validity. Clear any stale one from a prior attempt.
    (_INCOMING / "pending.sha256").unlink(missing_ok=True)
    message = _run_activator("apply")
    # The apply detaches and will restart THIS backend shortly; the UI polls
    # /api/update/status (through the restart) to observe the outcome.
    return {"accepted": True, "message": message}


@router.post("/api/update/rollback")
def update_rollback():
    if not config.APP_ROOT.is_dir():
        raise HTTPException(400, "this device has no updatable app partition")
    message = _run_activator("rollback")
    return {"accepted": True, "message": message}
