# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
"""Which box the backend is running on, detected at runtime.

The mirror of `src/util/Platform.h` on the renderer side, and it must stay
in step with it: same source file, same tier slugs, same override env var,
so the two halves of the app can never disagree about what they're running
on. The backend detects independently rather than reading the renderer's
status.json, so the tier is still reportable while the renderer is down --
which is exactly when support wants to know what the hardware is.

Policy (docs/distribution.md, "Hardware support"): ADVISORY only. The tier
is reported so the editor can label the box and so per-model values have
somewhere to live; it does NOT change the layer budget or anything else
that would make a show authored on one Pi unplayable on another.
"""

import os
from functools import lru_cache
from pathlib import Path

_MODEL_PATH = Path("/proc/device-tree/model")

_TIERS = ("desktop", "pi3", "pi4", "pi5", "pi")


def _read_model() -> str:
    """The device-tree model node is a NUL-terminated property, not a text
    file -- strip the trailing NUL (and any newline) or it lands raw in JSON."""
    try:
        raw = _MODEL_PATH.read_bytes()
    except OSError:
        return ""
    return raw.decode("utf-8", "replace").rstrip("\x00\n ").strip()


def _tier_from_model(model: str) -> str:
    if "Raspberry Pi" not in model:
        return "desktop"
    # Compute Modules spell the generation after "Compute Module", so the
    # plain "Raspberry Pi <n>" test misses them -- check both.
    for generation in ("5", "4", "3"):
        if f"Raspberry Pi {generation}" in model or f"Compute Module {generation}" in model:
            return f"pi{generation}"
    return "pi"


@lru_cache(maxsize=1)
def detect() -> dict:
    """{"tier", "model", "forced"} -- cached, since the model can't change
    under a running process. LIVEPI_HARDWARE_TIER overrides the detection
    (same escape hatch the renderer has) so a tier can be exercised on a dev
    box; an unrecognized value is ignored rather than trusted."""
    model = _read_model()
    tier = _tier_from_model(model)
    forced = False

    override = os.environ.get("LIVEPI_HARDWARE_TIER", "").strip()
    if override in _TIERS:
        tier, forced = override, True

    return {"tier": tier, "model": model or "desktop", "forced": forced}
