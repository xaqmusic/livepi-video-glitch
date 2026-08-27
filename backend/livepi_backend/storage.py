"""Show/library file I/O. Every write is atomic (tempfile in the same
directory + os.replace) so the renderer's per-frame mtime poll can never
catch a half-written JSON -- the same contract its hot-reload relies on."""

import json
import os
import tempfile
from pathlib import Path

from . import config


def atomic_write_json(path: Path, data) -> None:
    """Write + rename, and make it DURABLE.

    os.replace alone is atomic against a concurrent READER -- nobody ever sees a
    half-written file -- but it is not durable against power loss: the contents
    and the rename both sit in the page cache until ext4 commits, up to seconds
    later. That gap matters more here than on a normal machine, because this
    appliance is DESIGNED to have its power pulled (read-only root, no clean
    shutdown expected) and the Pi 5's power button force-cuts at ~5s. A setting
    saved seconds before the operator kills power must not evaporate -- which is
    exactly what was observed: a gear-menu change made moments before a hard
    power-off came back with the old value.

    So: fsync the file before the rename, then fsync the DIRECTORY so the rename
    entry itself is on disk. These are rare, operator-initiated writes (settings,
    shows, the clip library), so the cost is irrelevant next to silently losing
    one."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(data, f, indent=4)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
        dir_fd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(dir_fd)
        finally:
            os.close(dir_fd)
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
