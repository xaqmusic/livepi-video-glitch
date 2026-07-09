"""Show/library file I/O. Every write is atomic (tempfile in the same
directory + os.replace) so the renderer's per-frame mtime poll can never
catch a half-written JSON -- the same contract its hot-reload relies on."""

import json
import os
import tempfile
from pathlib import Path

from . import config


def atomic_write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(data, f, indent=4)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass
        raise


def read_json(path: Path):
    with open(path) as f:
        return json.load(f)


def show_path(name: str) -> Path:
    # Names come from URLs -- refuse anything that could escape shows/.
    if not name or "/" in name or name.startswith("."):
        raise ValueError(f"Invalid show name: {name!r}")
    return config.SHOWS_DIR / f"{name}.json"


def list_shows() -> list[str]:
    if not config.SHOWS_DIR.is_dir():
        return []
    return sorted(p.stem for p in config.SHOWS_DIR.glob("*.json") if p.stem != "active")


def get_active_show_name() -> str | None:
    active = config.SHOWS_DIR / "active.json"
    if not active.exists():
        return None
    return read_json(active).get("activeShow")


def set_active_show_name(name: str) -> None:
    atomic_write_json(config.SHOWS_DIR / "active.json", {"activeShow": name})


def read_library() -> dict:
    if not config.LIBRARY_PATH.exists():
        return {"clips": []}
    return read_json(config.LIBRARY_PATH)


def write_library(library: dict) -> None:
    atomic_write_json(config.LIBRARY_PATH, library)
