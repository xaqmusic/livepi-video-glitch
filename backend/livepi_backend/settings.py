"""Device-global settings the owner controls from the gear menu -- NOT
per-show (those live in the show JSON). Two things for v1:

  * showCardOnBoot -- whether the on-screen setup/QR card appears on every
    boot. The renderer's signal is the .claimed marker's existence
    (auth.CLAIMED_PATH): present => hide. "Show on boot" is the inverse. It
    latches off automatically on first login; this lets the owner turn it back
    on (e.g. to help someone connect at a new venue) and have that choice stick
    across later logins.
  * sceneAdvance / sceneBack -- an optional MIDI note or CC bound to scene
    switching, learned in the gear menu. Persisted here and mirrored to a file
    the renderer watches (see controls.py in Pass 2 / SceneControlMap).

Persisted on DATA_DIR (config.SETTINGS_PATH) so it survives deploys and the
read-only root, like auth.json.
"""

from fastapi import APIRouter, Depends
from pydantic import BaseModel

from . import config, storage
from .auth import CLAIMED_PATH, require_session

router = APIRouter(dependencies=[Depends(require_session)])


def _read() -> dict:
    return storage.read_json(config.SETTINGS_PATH) if config.SETTINGS_PATH.is_file() else {}


def _card_on_boot() -> bool:
    # Effective state is the renderer's own signal: the card shows on boot iff
    # the .claimed marker is absent.
    return not CLAIMED_PATH.exists()


@router.get("/api/settings")
def get_settings():
    return {"showCardOnBoot": _card_on_boot()}


class SettingsPatch(BaseModel):
    showCardOnBoot: bool | None = None


@router.post("/api/settings")
def update_settings(patch: SettingsPatch):
    stored = _read()
    if patch.showCardOnBoot is not None:
        # Keep the renderer's marker in sync with the choice, AND record the
        # choice so auth.login() stops auto-latching over it.
        try:
            CLAIMED_PATH.parent.mkdir(parents=True, exist_ok=True)
            if patch.showCardOnBoot:
                CLAIMED_PATH.unlink(missing_ok=True)  # show on boot -> no marker
            else:
                CLAIMED_PATH.touch()                   # hide -> marker present
        except OSError:
            pass
        stored["showCardOnBoot"] = patch.showCardOnBoot
        storage.atomic_write_json(config.SETTINGS_PATH, stored)
    return {"ok": True, "showCardOnBoot": _card_on_boot()}
