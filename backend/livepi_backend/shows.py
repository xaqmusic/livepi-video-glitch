"""Show CRUD, whole-document model (docs/videosynth-backend.md): a show is
one small cohesively-edited JSON, so GET returns the full document and PUT
replaces it -- no per-scene endpoints. Writes validate first and land
atomically; the renderer hot-reloads within a frame of the rename."""

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, ValidationError

from . import storage
from .auth import require_session
from .validation import ValidationProblem, validate_show

router = APIRouter(dependencies=[Depends(require_session)])


class CreateShowBody(BaseModel):
    name: str
    copyFrom: str | None = None


class SetActiveBody(BaseModel):
    name: str


def _empty_show() -> dict:
    return {"schemaVersion": 1, "scenes": []}


@router.get("/api/shows")
def list_shows():
    return {"shows": storage.list_shows(), "active": storage.get_active_show_name()}


@router.get("/api/shows/{name}")
def get_show(name: str):
    try:
        path = storage.show_path(name)
    except ValueError as e:
        raise HTTPException(400, str(e))
    if not path.exists():
        raise HTTPException(404, f"No show named {name!r}")
    return storage.read_json(path)


@router.put("/api/shows/{name}")
def put_show(name: str, document: dict):
    try:
        path = storage.show_path(name)
    except ValueError as e:
        raise HTTPException(400, str(e))
    try:
        show, warnings = validate_show(document)
    except ValidationError as e:
        raise HTTPException(422, detail=[str(err["msg"]) for err in e.errors()])
    except ValidationProblem as e:
        raise HTTPException(422, detail=e.errors)
    # Persist the SANITIZED document (retired params/mappings stripped).
    storage.atomic_write_json(path, show.model_dump())
    return {"ok": True, "warnings": warnings}


@router.post("/api/shows")
def create_show(body: CreateShowBody):
    try:
        path = storage.show_path(body.name)
    except ValueError as e:
        raise HTTPException(400, str(e))
    if path.exists():
        raise HTTPException(409, f"Show {body.name!r} already exists")

    if body.copyFrom:
        try:
            src = storage.show_path(body.copyFrom)
        except ValueError as e:
            raise HTTPException(400, str(e))
        if not src.exists():
            raise HTTPException(404, f"No show named {body.copyFrom!r}")
        document = storage.read_json(src)
    else:
        document = _empty_show()

    storage.atomic_write_json(path, document)
    return {"ok": True, "name": body.name}


@router.delete("/api/shows/{name}")
def delete_show(name: str):
    try:
        path = storage.show_path(name)
    except ValueError as e:
        raise HTTPException(400, str(e))
    if not path.exists():
        raise HTTPException(404, f"No show named {name!r}")
    if storage.get_active_show_name() == name:
        raise HTTPException(409, "Refusing to delete the active show -- switch first")
    path.unlink()
    return {"ok": True}


@router.get("/api/shows-active")
def get_active():
    return {"active": storage.get_active_show_name()}


@router.post("/api/shows-active")
def set_active(body: SetActiveBody):
    try:
        path = storage.show_path(body.name)
    except ValueError as e:
        raise HTTPException(400, str(e))
    if not path.exists():
        raise HTTPException(404, f"No show named {body.name!r}")
    storage.set_active_show_name(body.name)
    return {"ok": True, "active": body.name}
