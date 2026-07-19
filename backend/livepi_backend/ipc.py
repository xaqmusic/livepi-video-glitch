"""One-way writes into the renderer's command FIFO (docs/videosynth-backend.md
IPC section). Shared by the /api/command route and anything else that nudges the
renderer -- e.g. auth hiding the on-screen setup card once someone logs in. Kept
in its own module so both commands.py and auth.py can use it without an import
cycle (commands.py imports auth for require_session)."""

import os

from . import config


def send_command_line(line: str) -> bool:
    """Write one command line to the renderer FIFO, non-blocking. Returns False
    and NEVER raises when the write can't land -- the renderer isn't reading
    (ENXIO), there's no FIFO (ENOENT), or anything else. Best-effort nudges
    (auth hiding the card on login) ignore the result; the /api/command route
    turns False into a 503."""
    try:
        fd = os.open(str(config.COMMAND_FIFO), os.O_WRONLY | os.O_NONBLOCK)
    except OSError:
        return False
    try:
        os.write(fd, (line.rstrip("\n") + "\n").encode())
        return True
    except OSError:
        return False
    finally:
        os.close(fd)
