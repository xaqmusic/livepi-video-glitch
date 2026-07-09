"""GET /api/effects -- the hand-maintained manifest (backend doc: fine at
a dozen effects; generate from the real passes only if it gets annoying)."""

import json
from pathlib import Path

from fastapi import APIRouter, Depends

from .auth import require_session

_MANIFEST_PATH = Path(__file__).resolve().parents[1] / "effects_manifest.json"

router = APIRouter(dependencies=[Depends(require_session)])


def load_manifest() -> dict:
    with open(_MANIFEST_PATH) as f:
        return json.load(f)


@router.get("/api/effects")
def get_effects():
    return load_manifest()
