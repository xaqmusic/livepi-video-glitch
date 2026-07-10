"""Environment-driven configuration, .env pattern (backend/.env on the Pi
via the systemd unit's EnvironmentFile; plain env vars in dev). Defaults
point at the repo's own bin/data so `scripts/run-backend-dev.sh` and the
renderer share files exactly like they do on the Pi."""

import os
import platform
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]

DATA_DIR = Path(os.environ.get("LIVEPI_DATA_DIR", _REPO_ROOT / "bin" / "data"))
SHOWS_DIR = DATA_DIR / "shows"
CLIPS_DIR = DATA_DIR / "clips"
LIBRARY_PATH = CLIPS_DIR / "library.json"
THUMBS_DIR = CLIPS_DIR / ".thumbs"
# Baked ping-pong "boomerangs" (forward segment + pre-reversed segment as one
# forward-looping file -- the Pi's decoder can't play rate -1). Derived, per
# (clip, trim); the renderer finds them by filename convention.
PINGPONG_DIR = CLIPS_DIR / ".pingpong"

STATUS_PATH = Path(os.environ.get("LIVEPI_STATUS_PATH", "/tmp/livepi/status.json"))
COMMAND_FIFO = Path(os.environ.get("LIVEPI_COMMAND_FIFO", "/tmp/livepi/command.fifo"))

# Shared password for the session cookie -- LAN-only single-user gear, per
# docs/videosynth-backend.md. Change it in backend/.env for anything beyond
# the home network.
PASSWORD = os.environ.get("LIVEPI_PASSWORD", "livepi")
SECRET_KEY = os.environ.get("LIVEPI_SECRET_KEY", "livepi-dev-secret-change-me")

PORT = int(os.environ.get("LIVEPI_PORT", "8080"))

# Frontend build output served as static files (Phase C).
FRONTEND_DIST = _REPO_ROOT / "frontend" / "dist"

# ffmpeg gets throttled hard on the Pi so a transcode never fights the
# renderer for cores (measured: renderer needs ~2 cores worth at 30fps on
# heavy scenes).
IS_ARM = platform.machine() in ("aarch64", "armv7l")
FFMPEG_PRESET = "veryfast" if IS_ARM else "medium"
FFMPEG_THREADS = 2 if IS_ARM else 0  # 0 = ffmpeg default
FFMPEG_NICE = 19

# ffmpeg's `reverse` filter buffers the whole segment in RAM (~3MB/frame at
# 1080p), so a boomerang's trimmed segment is length-capped -- ping-pong
# clips are short by nature. Beyond this the prep endpoint refuses and asks
# for a tighter trim.
PINGPONG_MAX_SECONDS = float(os.environ.get("LIVEPI_PINGPONG_MAX_SECONDS", "15"))
