"""Upload ingest: ffprobe gate + single-worker transcode queue.

Compliant files (H.264, yuv420p, height <= 1080) pass straight through;
everything else (phone HEVC, 4K, odd pixel formats) gets the
scripts/import-clip.sh recipe -- ported here as the single source of truth
for server-side ingest -- with progress parsed from `ffmpeg -progress`.
One job at a time, `nice -19` + capped threads on the Pi, so a transcode
never fights the renderer for cores (the Phase B verify watches renderer
fps during a transcode to prove it)."""

import json
import queue
import re
import subprocess
import threading
import uuid
from dataclasses import dataclass, field
from pathlib import Path

from . import config


@dataclass
class Job:
    id: str
    src: Path
    dest: Path
    display_name: str
    state: str = "queued"  # queued | probing | transcoding | done | error
    progress: float = 0.0
    error: str | None = None
    clip: dict | None = None
    lock: threading.Lock = field(default_factory=threading.Lock)


_jobs: dict[str, Job] = {}
_queue: "queue.Queue[Job]" = queue.Queue()
_worker_started = False
_worker_lock = threading.Lock()


def probe(path: Path) -> dict | None:
    # stdbuf -o0 + ignoring the exit code: Trixie's libx265 4.1 executes
    # SVE instructions the Pi 4's Cortex-A72 doesn't have, SIGILLing every
    # ffprobe/ffmpeg AT EXIT (in libx265's teardown, after all real work
    # is done). Block-buffered stdout dies with the process, so unbuffer
    # it and judge by whether parseable JSON arrived, not by returncode.
    try:
        out = subprocess.run(
            [
                "stdbuf", "-o0", "ffprobe", "-v", "error",
                "-show_entries", "stream=codec_type,codec_name,pix_fmt,width,height,duration",
                "-show_entries", "format=duration",
                "-of", "json", str(path),
            ],
            capture_output=True, text=True, timeout=30,
        )
        data = json.loads(out.stdout)
        streams = data.get("streams", [])
        video = next(s for s in streams if s.get("codec_type") == "video")
        duration = video.get("duration") or data.get("format", {}).get("duration") or 0
        return {
            "codec": video.get("codec_name", ""),
            "pixFmt": video.get("pix_fmt", ""),
            "width": int(video.get("width", 0)),
            "height": int(video.get("height", 0)),
            "duration": float(duration),
            "hasAudio": any(s.get("codec_type") == "audio" for s in streams),
        }
    except Exception:
        return None


def video_compliant(info: dict) -> bool:
    return (
        info["codec"] == "h264"
        and info["pixFmt"] == "yuv420p"
        and info["height"] <= 1080
    )


def is_compliant(info: dict) -> bool:
    # Audio disqualifies passthrough: the renderer never plays clip audio,
    # and an audio track reaching playbin on the kiosk (which has no
    # working audio sink) can kill the whole pipeline. Compliant-but-audio
    # files get a lossless video-copy remux that strips it.
    return video_compliant(info) and not info["hasAudio"]


def make_thumbnail(clip_path: Path, clip_id: str) -> str | None:
    config.THUMBS_DIR.mkdir(parents=True, exist_ok=True)
    thumb = config.THUMBS_DIR / f"{clip_id}.jpg"
    subprocess.run(
        [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-ss", "1", "-i", str(clip_path),
            "-frames:v", "1", "-vf", "scale=320:-2", str(thumb),
        ],
        capture_output=True, timeout=60,
    )
    # Judged by the artifact, not the exit code (libx265 teardown SIGILL,
    # see probe()).
    return thumb.name if thumb.exists() and thumb.stat().st_size > 0 else None


def _transcode(job: Job, video_copy: bool = False) -> None:
    info = probe(job.src)
    duration_us = (info["duration"] if info else 0) * 1_000_000 or 1

    if video_copy:
        # Compliant video that just needs its audio stripped: lossless
        # stream copy, effectively instant.
        video_args = ["-c:v", "copy"]
    else:
        # scripts/import-clip.sh's recipe (keep in sync), plus Pi throttling.
        video_args = [
            "-vf", "scale=-2:'min(1080,ih)'",
            "-c:v", "libx264", "-profile:v", "high", "-preset", config.FFMPEG_PRESET,
            "-crf", "20", "-pix_fmt", "yuv420p", "-g", "30",
        ]
    cmd = [
        "nice", "-n", str(config.FFMPEG_NICE),
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(job.src),
        *video_args,
        "-an", "-movflags", "+faststart",
        "-progress", "pipe:1",
    ]
    if config.FFMPEG_THREADS and not video_copy:
        cmd += ["-threads", str(config.FFMPEG_THREADS)]
    cmd.append(str(job.dest))

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    saw_end = False
    for line in proc.stdout:
        line = line.strip()
        m = re.match(r"out_time_us=(\d+)", line)
        if m:
            with job.lock:
                job.progress = min(0.99, int(m.group(1)) / duration_us)
        if line == "progress=end":
            saw_end = True
    proc.wait()
    # `progress=end` means ffmpeg finished writing the output -- a nonzero
    # exit AFTER that is the libx265 teardown SIGILL (see probe()), not a
    # failed transcode.
    if proc.returncode != 0 and not (saw_end and job.dest.is_file()):
        raise RuntimeError(proc.stderr.read()[-500:] if proc.stderr else "ffmpeg failed")


def _register_clip(job: Job) -> dict:
    from . import storage  # late import to avoid cycles

    clip_id = f"clip-{uuid.uuid4().hex[:8]}"
    info = probe(job.dest) or {}
    thumb = make_thumbnail(job.dest, clip_id)
    clip = {
        "id": clip_id,
        "path": f"clips/{job.dest.name}",
        "name": job.display_name,
        "width": info.get("width", 0),
        "height": info.get("height", 0),
        "duration": info.get("duration", 0),
        "thumb": thumb,
    }
    library = storage.read_library()
    library.setdefault("clips", []).append(clip)
    storage.write_library(library)
    return clip


def _worker() -> None:
    while True:
        job = _queue.get()
        try:
            with job.lock:
                job.state = "probing"
            info = probe(job.src)
            if info is None:
                raise RuntimeError("Not a readable video file")

            if is_compliant(info):
                job.src.rename(job.dest)
                with job.lock:
                    job.progress = 1.0
            else:
                with job.lock:
                    job.state = "transcoding"
                _transcode(job, video_copy=video_compliant(info))
                job.src.unlink(missing_ok=True)

            clip = _register_clip(job)
            with job.lock:
                job.state = "done"
                job.progress = 1.0
                job.clip = clip
        except Exception as e:
            job.dest.unlink(missing_ok=True)
            job.src.unlink(missing_ok=True)
            with job.lock:
                job.state = "error"
                job.error = str(e)


def _ensure_worker() -> None:
    global _worker_started
    with _worker_lock:
        if not _worker_started:
            threading.Thread(target=_worker, daemon=True).start()
            _worker_started = True


def enqueue(src: Path, dest: Path, display_name: str) -> Job:
    _ensure_worker()
    job = Job(id=uuid.uuid4().hex[:12], src=src, dest=dest, display_name=display_name)
    _jobs[job.id] = job
    _queue.put(job)
    return job


def get_job(job_id: str) -> Job | None:
    return _jobs.get(job_id)
