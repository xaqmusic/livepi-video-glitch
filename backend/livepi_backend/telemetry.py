"""WS /ws/telemetry -- relays the renderer's status file (Learn mode's CC
feed, the heaviness indicator, Live mode's current scene). Reads the file
only while a client is connected, re-parsing only when its mtime moves --
idle cost is zero, per the backend doc."""

import asyncio
import json
from pathlib import Path

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from . import config
from .auth import COOKIE_NAME, is_valid_token

router = APIRouter()

_POLL_SECS = 0.1


@router.websocket("/ws/telemetry")
async def telemetry(ws: WebSocket):
    if not is_valid_token(ws.cookies.get(COOKIE_NAME)):
        await ws.close(code=4401)
        return
    await ws.accept()

    path = Path(config.STATUS_PATH)
    last_mtime = 0.0
    try:
        while True:
            try:
                mtime = path.stat().st_mtime
            except FileNotFoundError:
                mtime = 0.0
            if mtime and mtime != last_mtime:
                last_mtime = mtime
                try:
                    with open(path) as f:
                        payload = json.load(f)
                    await ws.send_json(payload)
                except (json.JSONDecodeError, OSError):
                    pass  # atomic rename makes this near-impossible; skip a beat if it happens
            await asyncio.sleep(_POLL_SECS)
    except WebSocketDisconnect:
        pass
