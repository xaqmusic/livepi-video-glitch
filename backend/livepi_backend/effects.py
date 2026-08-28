# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
"""GET /api/effects -- the hand-maintained manifest (backend doc: fine at
a dozen effects; generate from the real passes only if it gets annoying)."""

import json
from pathlib import Path

from fastapi import APIRouter, Depends

from .auth import require_session
from .hardware import detect as detect_hardware

_MANIFEST_PATH = Path(__file__).resolve().parents[1] / "effects_manifest.json"

router = APIRouter(dependencies=[Depends(require_session)])


def load_manifest() -> dict:
    with open(_MANIFEST_PATH) as f:
        return json.load(f)


@router.get("/api/effects")
def get_effects():
    manifest = load_manifest()
    # The detected box, served alongside layerBudget because that's where a
    # per-model value would eventually live. Advisory only today: the budget
    # itself stays the Pi 4's measured one on every tier, so a show authored
    # on a Pi 5 still plays on a Pi 4 (docs/distribution.md).
    manifest["hardware"] = detect_hardware()
    return manifest
