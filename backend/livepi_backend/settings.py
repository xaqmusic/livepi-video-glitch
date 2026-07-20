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

from typing import Literal

from fastapi import APIRouter, Depends
from pydantic import BaseModel, Field

from . import config, storage
from .auth import CLAIMED_PATH, require_session

router = APIRouter(dependencies=[Depends(require_session)])


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
