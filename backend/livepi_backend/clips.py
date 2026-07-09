"""Clip library: list with metadata + thumbnails, multipart upload with
auto-transcode (decision: phone clips just work), delete with cheap
referential integrity (refuse while any show references the clip)."""

import re
import shutil
import uuid
from pathlib import Path

from fastapi import APIRouter, Depends, HTTPException, UploadFile
from fastapi.responses import FileResponse

from . import config, storage, transcode
from .auth import require_session

router = APIRouter(dependencies=[Depends(require_session)])


def _safe_stem(filename: str) -> str:
    stem = Path(filename).stem
    stem = re.sub(r"[^A-Za-z0-9._-]+", "_", stem) or "clip"
    return stem[:60]


@router.get("/api/clips")
def list_clips():
    library = storage.read_library()
    clips = []
    for clip in library.get("clips", []):
        entry = dict(clip)
        entry["exists"] = (config.DATA_DIR / clip["path"]).exists()
        if clip.get("thumb"):
            entry["thumbUrl"] = f"/api/clips/{clip['id']}/thumb"
        clips.append(entry)
    return {"clips": clips}


@router.get("/api/clips/{clip_id}/thumb")
def get_thumb(clip_id: str):
    library = storage.read_library()
    for clip in library.get("clips", []):
        if clip["id"] == clip_id and clip.get("thumb"):
            path = config.THUMBS_DIR / clip["thumb"]
            if path.exists():
                return FileResponse(path, media_type="image/jpeg")
    raise HTTPException(404, "No thumbnail")


@router.post("/api/clips")
def upload_clip(file: UploadFile):
    config.CLIPS_DIR.mkdir(parents=True, exist_ok=True)
    stem = _safe_stem(file.filename or "clip")
    suffix = uuid.uuid4().hex[:6]
    # Spool the upload next to its destination (same filesystem -> the
    # compliant-passthrough rename is atomic and instant).
    spool = config.CLIPS_DIR / f".upload-{suffix}-{stem}"
    dest = config.CLIPS_DIR / f"{stem}-{suffix}.mp4"
    with open(spool, "wb") as out:
        shutil.copyfileobj(file.file, out)

    job = transcode.enqueue(spool, dest, display_name=stem)
    return {"jobId": job.id}


@router.get("/api/clips/jobs/{job_id}")
def job_status(job_id: str):
    job = transcode.get_job(job_id)
    if job is None:
        raise HTTPException(404, "No such job")
    with job.lock:
        return {
            "state": job.state,
            "progress": job.progress,
            "error": job.error,
            "clip": job.clip,
        }


@router.delete("/api/clips/{clip_id}")
def delete_clip(clip_id: str):
    library = storage.read_library()
    clips = library.get("clips", [])
    match = next((c for c in clips if c["id"] == clip_id), None)
    if match is None:
        raise HTTPException(404, "No such clip")

    # Cheap referential integrity: scan every show for references.
    for name in storage.list_shows():
        show = storage.read_json(storage.show_path(name))
        for scene in show.get("scenes", []):
            for layer in scene.get("layers", []):
                if layer.get("kind") == "clip" and layer.get("source") == clip_id:
                    raise HTTPException(
                        409, f'Clip is used by show "{name}" scene "{scene.get("name")}"'
                    )

    library["clips"] = [c for c in clips if c["id"] != clip_id]
    storage.write_library(library)
    (config.DATA_DIR / match["path"]).unlink(missing_ok=True)
    if match.get("thumb"):
        (config.THUMBS_DIR / match["thumb"]).unlink(missing_ok=True)
    return {"ok": True}
