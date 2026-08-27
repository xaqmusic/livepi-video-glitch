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
        "netInstallPrompt": (
            stored["netInstallPrompt"] if "netInstallPrompt" in stored else _net_install()
        ),
    }


@router.get("/api/settings")
def get_settings():
    return _public()


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
