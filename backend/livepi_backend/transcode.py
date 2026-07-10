"""Upload ingest: ffprobe gate + single-worker transcode queue.

Compliant files (H.264, yuv420p, height <= 1080) pass straight through;
everything else (phone HEVC, 4K, odd pixel formats) gets the
scripts/import-clip.sh recipe -- ported here as the single source of truth
for server-side ingest -- with progress parsed from `ffmpeg -progress`.
One job at a time, `nice -19` + capped threads on the Pi, so a transcode
never fights the renderer for cores (the Phase B verify watches renderer
fps during a transcode to prove it)."""

import json
import os
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
    # "ingest": src is an upload spool, dest is the new library file.
    # "intra": src IS an existing library clip; dest is a temp that
    # replaces it after an all-intra re-encode (smooth reverse prep).
    # "pingpong": src is an existing library clip; dest is a baked boomerang
    # (forward+reverse of [start,end]) the renderer loops forward.
    mode: str = "ingest"
    clip_id: str | None = None
    start: float = 0.0  # normalized trim, "pingpong" mode
    end: float = 1.0
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


def pingpong_key(x: float) -> int:
    """Quantize a normalized trim position to a stable integer key (0..1000,
    0.001 resolution). The boomerang filename is `<stem>__<kStart>-<kEnd>.mp4`;
    the renderer (ShowLoader) and the UI compute this identically to find the
    file by convention -- keep the three in sync (+0.5 rounding, double math)."""
    return int(min(1.0, max(0.0, x)) * 1000 + 0.5)


def boomerang_path(clip_path: str, start: float, end: float) -> Path:
    """Deterministic boomerang location for a (clip, trim) pair."""
    stem = Path(clip_path).stem
    return config.PINGPONG_DIR / f"{stem}__{pingpong_key(start):04d}-{pingpong_key(end):04d}.mp4"


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


def _transcode(job: Job, video_copy: bool = False, intra: bool = False) -> None:
    info = probe(job.src)
    duration_us = (info["duration"] if info else 0) * 1_000_000 or 1

    if intra:
        # All-intra re-encode (-g 1: every frame a keyframe). The Pi's
        # hardware decoder can't step backwards through GOPs, so ping-pong
        # reverse stutters on normal files; all-intra makes every frame
        # independently decodable -- smooth reverse, and loop-wrap seeks
        # land instantly (no GOP re-decode). Costs bitrate, so it's a
        # per-clip opt-in, not the ingest default.
        video_args = [
            "-c:v", "libx264", "-profile:v", "high", "-preset", config.FFMPEG_PRESET,
            "-crf", "21", "-pix_fmt", "yuv420p", "-g", "1",
        ]
    elif video_copy:
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


def _boomerang(job: Job, info: dict) -> None:
    # Bake [start,end] forward + the same segment reversed into ONE file that
    # plays as an ordinary forward loop. The Pi's v4l2 decoder stalls on rate
    # -1 (any GOP structure), so ping-pong can't decode backwards -- it plays
    # pre-reversed frames instead. The reversal now lives INSIDE the clip, so
    # the freeze at the turnaround is gone; the only seam left is the normal
    # loop wrap at the far end (once per full cycle).
    duration = info.get("duration", 0.0)
    start_sec = job.start * duration
    end_sec = job.end * duration
    seg_sec = max(0.0, end_sec - start_sec)
    # trim (frame-accurate, in the graph) -> split -> reverse one copy ->
    # concat forward+reverse. `reverse` buffers the segment in RAM; the
    # endpoint caps segment length so this stays bounded.
    filt = (
        f"[0:v]trim=start={start_sec:.4f}:end={end_sec:.4f},setpts=PTS-STARTPTS,split[a][b];"
        f"[b]reverse[r];[a][r]concat=n=2:v=1[out]"
    )
    cmd = [
        "nice", "-n", str(config.FFMPEG_NICE),
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(job.src),
        "-filter_complex", filt,
        "-map", "[out]", "-an",
        "-c:v", "libx264", "-profile:v", "high", "-preset", config.FFMPEG_PRESET,
        "-crf", "20", "-pix_fmt", "yuv420p", "-g", "15",
        "-movflags", "+faststart",
        "-progress", "pipe:1",
    ]
    if config.FFMPEG_THREADS:
        cmd += ["-threads", str(config.FFMPEG_THREADS)]
    cmd.append(str(job.dest))
    # Output is 2x the segment (forward + reverse).
    duration_us = max(1, int(2 * seg_sec * 1_000_000))

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


def enqueue_intra(clip: dict) -> "Job":
    """Queue an all-intra re-encode of an EXISTING library clip in place
    (smooth-reverse prep). Runs through the same single-worker queue and
    shows in the jobs banner like any transcode."""
    src = config.DATA_DIR / clip["path"]
    dest = src.with_name(src.stem + ".intra-tmp.mp4")
    job = Job(
        id=uuid.uuid4().hex[:12],
        src=src,
        dest=dest,
        display_name=(clip.get("name") or clip["id"]) + " (smooth reverse)",
        mode="intra",
        clip_id=clip["id"],
    )
    _jobs[job.id] = job
    _ensure_worker()
    _queue.put(job)
    return job


def enqueue_pingpong(clip: dict, start: float, end: float) -> "Job":
    """Queue a boomerang bake for an EXISTING library clip's [start,end]
    segment. Output lands at the deterministic boomerang_path so the renderer
    finds it by convention; on success the (kStart,kEnd) key is recorded on
    the clip for the UI's "baked" indicator."""
    config.PINGPONG_DIR.mkdir(parents=True, exist_ok=True)
    src = config.DATA_DIR / clip["path"]
    dest = boomerang_path(clip["path"], start, end)
    job = Job(
        id=uuid.uuid4().hex[:12],
        src=src,
        dest=dest,
        display_name=(clip.get("name") or clip["id"]) + " (ping-pong bake)",
        mode="pingpong",
        clip_id=clip["id"],
        start=start,
        end=end,
    )
    _jobs[job.id] = job
    _ensure_worker()
    _queue.put(job)
    return job


def prune_boomerangs(clip_path: str) -> None:
    """Delete every baked boomerang for a clip (used when the clip itself is
    deleted). Silent on missing files."""
    stem = Path(clip_path).stem
    if config.PINGPONG_DIR.is_dir():
        for f in config.PINGPONG_DIR.glob(f"{stem}__*.mp4"):
            f.unlink(missing_ok=True)


def list_jobs() -> list[dict]:
    """All jobs this backend process has seen, oldest first -- the UI's
    global activity banner filters for the live ones."""
    out = []
    for job in _jobs.values():
        with job.lock:
            out.append({
                "id": job.id,
                "name": job.display_name,
                "state": job.state,
                "progress": job.progress,
                "error": job.error,
            })
    return out


def _mark_clip_intra(clip_id: str | None) -> dict | None:
    from . import storage  # late import to avoid cycles

    library = storage.read_library()
    for clip in library.get("clips", []):
        if clip["id"] == clip_id:
            clip["intra"] = True
            storage.write_library(library)
            return clip
    return None


def _record_pingpong(clip_id: str | None, start: float, end: float) -> dict | None:
    from . import storage  # late import to avoid cycles

    key = [pingpong_key(start), pingpong_key(end)]
    library = storage.read_library()
    for clip in library.get("clips", []):
        if clip["id"] == clip_id:
            baked = [tuple(k) for k in clip.get("pingpong", [])]
            if tuple(key) not in baked:
                baked.append(tuple(key))
            # Keep only the most recent few keys; prune the boomerang files
            # for keys we drop so trim-tweaking doesn't accumulate footage.
            baked = baked[-8:]
            keep = {tuple(k) for k in baked}
            stem = Path(clip["path"]).stem
            if config.PINGPONG_DIR.is_dir():
                for f in config.PINGPONG_DIR.glob(f"{stem}__*.mp4"):
                    m = re.match(r".*__(\d+)-(\d+)$", f.stem)
                    if m and (int(m.group(1)), int(m.group(2))) not in keep:
                        f.unlink(missing_ok=True)
            clip["pingpong"] = [list(k) for k in baked]
            storage.write_library(library)
            return clip
    return None


def _worker() -> None:
    while True:
        job = _queue.get()
        try:
            with job.lock:
                job.state = "probing"
            info = probe(job.src)
            if info is None:
                raise RuntimeError("Not a readable video file")

            if job.mode == "intra":
                # Re-encode an existing clip all-intra IN PLACE: temp file
                # next to it, atomic replace on success. The library entry
                # keeps its id/path; only the "intra" flag flips.
                with job.lock:
                    job.state = "transcoding"
                _transcode(job, intra=True)
                os.replace(job.dest, job.src)
                clip = _mark_clip_intra(job.clip_id)
            elif job.mode == "pingpong":
                # Bake a boomerang for [start,end]; the clip file is untouched.
                with job.lock:
                    job.state = "transcoding"
                _boomerang(job, info)
                clip = _record_pingpong(job.clip_id, job.start, job.end)
            else:
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
            # Ingest jobs own their spool; intra jobs' src IS the library
            # clip -- never delete it on failure.
            if job.mode == "ingest":
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
