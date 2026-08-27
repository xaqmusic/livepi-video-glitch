"""Device-global settings the owner controls from the gear menu -- NOT
per-show (those live in the show JSON). For v1:

  * showCardOnBoot -- whether the on-screen setup/QR card appears on every
    boot. The renderer's signal is the .claimed marker's existence
    (auth.CLAIMED_PATH): present => hide. "Show on boot" is the inverse. It
    latches off automatically on first login; this lets the owner turn it back
    on (e.g. to help someone connect at a new venue) and have that choice stick
    across later logins.
  * sceneAdvance / sceneBack -- an optional MIDI note or CC bound to scene
    switching, learned in the gear menu. The renderer watches this same file
    (controls.scene_map -> SceneControlMap) and edge-detects the mapped control
    to fire a Click (next scene) / Hold (first scene).

Persisted on DATA_DIR (config.SETTINGS_PATH) so it survives deploys and the
read-only root, like auth.json.
"""

import subprocess
from typing import Literal

from fastapi import APIRouter, Depends
from pydantic import BaseModel, Field

from . import config, storage
from .auth import CLAIMED_PATH, require_session

router = APIRouter(dependencies=[Depends(require_session)])


def _net_install() -> bool | None:
    """True/False, or None where the board has no such setting (any non-Pi, and
    Pi models with no network-install prompt). None makes the gear menu hide the
    control rather than offer something that cannot work.

    Returns a BOOL, not the helper's "on"/"off" string: a non-empty string is
    truthy in JS, so leaking it through would render the checkbox ticked while
    the prompt was actually off."""
    try:
        out = subprocess.run(["sudo", "-n", str(config.NETINSTALL), "get"],
                             capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    value = out.stdout.strip()
    if value == "on":
        return True
    if value == "off":
        return False
    return None


def _set_net_install(enabled: bool) -> bool:
    """Apply the EEPROM change. True if it went through.

    Best-effort and non-fatal: a box where this is unavailable must still be
    able to save every OTHER setting on the page."""
    try:
        out = subprocess.run(["sudo", "-n", str(config.NETINSTALL), "on" if enabled else "off"],
                             capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return False
    return out.returncode == 0


def _image_clips() -> dict[str, str]:
    """clipId -> path, for every library entry that is a still image. Those are
    the only sensible splash candidates: a video would show one arbitrary frame."""
    try:
        library = storage.read_json(config.LIBRARY_PATH) if config.LIBRARY_PATH.is_file() else {}
    except (OSError, ValueError):
        return {}
    out = {}
    for clip in library.get("clips", []):
        path = clip.get("path", "")
        if path.lower().endswith((".png", ".jpg", ".jpeg")):
            out[clip["id"]] = path
    return out


def _splash_image(stored: dict) -> str:
    """The stored choice, but only while the file is still actually there."""
    path = stored.get("splashImage", "")
    if path and (config.DATA_DIR / path).is_file():
        return path
    return ""


def _read() -> dict:
    return storage.read_json(config.SETTINGS_PATH) if config.SETTINGS_PATH.is_file() else {}


def _card_on_boot() -> bool:
    # Effective state is the renderer's own signal: the card shows on boot iff
    # the .claimed marker is absent.
    return not CLAIMED_PATH.exists()


def _public(stored: dict | None = None) -> dict:
    stored = _read() if stored is None else stored
    return {
        "showCardOnBoot": _card_on_boot(),
        "sceneAdvance": stored.get("sceneAdvance"),
        "sceneBack": stored.get("sceneBack"),
        "thermalRescue": stored.get("thermalRescue", True),
        # Whether a mid-scene thermal step-down is masked by the scene's
        # transition (renderer reads this live). Default off (silent resize).
        "thermalTransition": stored.get("thermalTransition", False),
        # Live audio-reactivity tuning the renderer hot-reads from this file
        # (SceneControlMap): the overall-level one-pole smoothing coefficient
        # (0 = snappiest, ~0.95 = steadiest) and the adaptive-gain toggle (off =
        # unity gain, level set entirely by the Pisound's own input gain knob).
        "audioSmoothing": stored.get("audioSmoothing", 0.6),
        "audioAutoGain": stored.get("audioAutoGain", True),
        # INTENT first, hardware second. On a Pi 5 the EEPROM write lands in the
        # A/B inactive slot and only becomes readable after a reboot, so echoing
        # the hardware straight back would show the operator's toggle snapping
        # back to its old position. Our stored value is what WILL be in effect;
        # the EEPROM is the fallback for a box we have never set (including one
        # whose card was moved from another board, where the firmware is the
        # only truth available).
        # Boot splash: a path relative to DATA_DIR ("" = none). Stored here rather
        # than in app.json because the app tree is read-only on an appliance --
        # the renderer and the boot script both prefer this over the shipped
        # default. Cleared automatically if the clip it points at is gone, so a
        # deleted image cannot leave the box pointing at a missing file.
        "splashImage": _splash_image(stored),
        "netInstallPrompt": (
            stored["netInstallPrompt"] if "netInstallPrompt" in stored else _net_install()
        ),
    }


@router.get("/api/settings")
def get_settings():
    return _public()


@router.get("/api/settings/splash-candidates")
def splash_candidates():
    """Still images in the clip library, for the gear menu's splash picker.
    Videos are excluded -- a clip would only ever show one arbitrary frame."""
    clips = _image_clips()
    selected = _splash_image(_read())
    return {
        "clips": [{"id": cid, "path": path} for cid, path in clips.items()],
        "selectedId": next((cid for cid, p in clips.items() if p == selected), None),
    }


class SceneTrigger(BaseModel):
    type: Literal["cc", "note"]
    number: int = Field(ge=0, le=127)  # MIDI CC / note range


class SettingsPatch(BaseModel):
    # All optional. For the scene fields, ABSENT means "leave unchanged" while
    # an explicit null means "unbind" -- distinguished via model_fields_set.
    showCardOnBoot: bool | None = None
    thermalRescue: bool | None = None
    thermalTransition: bool | None = None
    audioSmoothing: float | None = Field(default=None, ge=0.0, le=0.98)
    audioAutoGain: bool | None = None
    netInstallPrompt: bool | None = None
    # A clipId from the library, or "" / null to clear it. Deliberately NOT a
    # free path: the web UI must not be able to point the renderer at an
    # arbitrary file on the box.
    splashClipId: str | None = None
    sceneAdvance: SceneTrigger | None = None
    sceneBack: SceneTrigger | None = None


@router.post("/api/settings")
def update_settings(patch: SettingsPatch):
    stored = _read()
    provided = patch.model_fields_set

    if patch.showCardOnBoot is not None:
        # Keep the renderer's marker in sync with the choice, AND record it so
        # auth.login() stops auto-latching over an explicit preference.
        try:
            CLAIMED_PATH.parent.mkdir(parents=True, exist_ok=True)
            if patch.showCardOnBoot:
                CLAIMED_PATH.unlink(missing_ok=True)  # show on boot -> no marker
            else:
                CLAIMED_PATH.touch()                   # hide -> marker present
        except OSError:
            pass
        stored["showCardOnBoot"] = patch.showCardOnBoot

    if patch.thermalRescue is not None:
        # Read live by the renderer from settings.json (SceneControlMap).
        stored["thermalRescue"] = patch.thermalRescue
    if patch.thermalTransition is not None:
        stored["thermalTransition"] = patch.thermalTransition

    if patch.audioSmoothing is not None:
        stored["audioSmoothing"] = patch.audioSmoothing
    if patch.audioAutoGain is not None:
        stored["audioAutoGain"] = patch.audioAutoGain

    if "splashClipId" in provided:
        clip_id = patch.splashClipId
        if not clip_id:
            stored.pop("splashImage", None)          # explicit clear
        else:
            path = _image_clips().get(clip_id)
            if path:
                stored["splashImage"] = path
            # An unknown id is ignored rather than raising: the library may have
            # changed under a stale editor tab, and losing one setting is better
            # than failing the whole save.

    if patch.netInstallPrompt is not None:
        # Only touch the EEPROM when the intent actually changes -- a settings
        # toggle should not rewrite firmware on every save.
        if stored.get("netInstallPrompt") != patch.netInstallPrompt:
            if _set_net_install(patch.netInstallPrompt):
                stored["netInstallPrompt"] = patch.netInstallPrompt

    for field in ("sceneAdvance", "sceneBack"):
        if field not in provided:
            continue  # absent -> leave whatever's stored
        value = getattr(patch, field)
        if value is None:
            stored.pop(field, None)                 # explicit null -> unbind
        else:
            stored[field] = value.model_dump()      # {type, number}

    if provided:
        storage.atomic_write_json(config.SETTINGS_PATH, stored)
    return {"ok": True, **_public(stored)}
