"""POST /api/command -> a line on the renderer's command FIFO. Non-blocking
write: if the renderer isn't running (no FIFO reader), the open fails with
ENXIO and the client gets an honest 503 instead of a hang."""

from typing import Literal, Optional

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field

from . import ipc
from .auth import require_session

router = APIRouter(dependencies=[Depends(require_session)])


class CommandBody(BaseModel):
    type: Literal["click", "hold", "debug", "goto", "cc", "note", "param", "card"]
    sceneId: Optional[str] = None
    number: Optional[int] = Field(default=None, ge=0, le=127)
    value: Optional[float] = None
    targetPath: Optional[str] = None
    action: Optional[Literal["on", "off", "toggle"]] = None  # for "card"


def _to_line(body: CommandBody) -> str:
    if body.type in ("click", "hold", "debug"):
        return body.type
    if body.type == "card":
        return f"card {body.action or 'toggle'}"
    if body.type == "goto":
        if not body.sceneId:
            raise HTTPException(422, "goto needs sceneId")
        return f"goto {body.sceneId}"
    if body.type in ("cc", "note"):
        if body.number is None or body.value is None:
            raise HTTPException(422, f"{body.type} needs number and value")
        return f"{body.type} {body.number} {body.value}"
    if body.type == "param":
        if not (body.sceneId and body.targetPath) or body.value is None:
            raise HTTPException(422, "param needs sceneId, targetPath, value")
        return f"param {body.sceneId} {body.targetPath} {body.value}"
    raise HTTPException(422, f"Unknown command type {body.type!r}")


@router.post("/api/command")
def send_command(body: CommandBody):
    if not ipc.send_command_line(_to_line(body)):
        raise HTTPException(503, "Renderer offline (command FIFO has no reader)")
    return {"ok": True}
