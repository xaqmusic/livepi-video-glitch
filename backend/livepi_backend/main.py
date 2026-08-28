# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
"""App factory: API routers + the built frontend served as static files
with an SPA fallback (any non-API path returns index.html so React Router
owns /edit, /live, etc.)."""

from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.responses import FileResponse, JSONResponse

from . import (
    auth,
    captive,
    clips,
    commands,
    config,
    effects,
    hardware,
    network,
    settings,
    shows,
    storage,
    telemetry,
    update,
)


def _default_show() -> dict:
    """The starter show a fresh box boots into: one full-screen plasma
    generator so first run is something alive and colourful, not a black
    screen. Plasma is cheap and aspect-tolerant (looks right on any panel),
    and the owner can edit or replace it from the web UI. Kept deliberately
    minimal -- one layer, no effects/mappings -- so it reads as a clean
    starting point."""
    return {
        "schemaVersion": 1,
        "scenes": [
            {
                "id": "scene-welcome",
                "name": "Welcome",
                "layers": [
                    {
                        "id": "layer-welcome",
                        "kind": "generator",
                        "source": "plasma",
                        "blendMode": "normal",
                        "opacity": 1.0,
                        "layerEffects": {},
                        "params": {},
                    }
                ],
                "mappings": [],
                "postEffects": {},
                "transition": {"style": "none", "duration": 0.8},
            }
        ],
    }


def _seed_data() -> None:
    """A fresh Pi (deploys exclude show data -- it's Pi-authored) still boots
    to something: the starter show above and an empty clip registry."""
    config.SHOWS_DIR.mkdir(parents=True, exist_ok=True)
    if storage.get_active_show_name() is None:
        if not (config.SHOWS_DIR / "default.json").exists():
            storage.atomic_write_json(config.SHOWS_DIR / "default.json", _default_show())
        storage.set_active_show_name("default")
    if not config.LIBRARY_PATH.exists():
        storage.write_library({"clips": []})


@asynccontextmanager
async def _lifespan(app: FastAPI):
    _seed_data()
    yield


app = FastAPI(title="LivePi Videosynth Backend", lifespan=_lifespan)

app.include_router(auth.router)
app.include_router(shows.router)
app.include_router(clips.router)
app.include_router(effects.router)
app.include_router(commands.router)
app.include_router(telemetry.router)
app.include_router(network.router)
app.include_router(settings.router)
app.include_router(update.router)
# Captive-portal probe responders -- before the SPA catch-all so the probe
# paths return a redirect rather than index.html.
app.include_router(captive.router)


@app.get("/api/health")
def health():
    return {"ok": True, "dataDir": str(config.DATA_DIR), "hardware": hardware.detect()}


# index.html must NEVER be cached: it names the hashed bundle, and a
# cached copy after a deploy serves stale JS against a new API/telemetry
# shape (first observed as Learn blanking the page after the lastControl
# rename). The hashed /assets are immutable by construction.
_NO_CACHE = {"Cache-Control": "no-cache, must-revalidate"}


# The dist check happens PER REQUEST, not at import: a long-running
# --reload dev server started before the first `vite build` used to lock
# in "Frontend not built" until manually restarted (uvicorn --reload only
# watches .py files). Serving files by hand here also replaces the
# /assets StaticFiles mount -- one code path, no import-time state.
@app.get("/{path:path}")
def spa(path: str):
    index = config.FRONTEND_DIST / "index.html"
    if not index.is_file():
        return JSONResponse(
            {"message": "Frontend not built -- API docs at /docs. Run `npm run build` in frontend/."},
            status_code=503,
        )
    candidate = (config.FRONTEND_DIST / path).resolve()
    if path and candidate.is_relative_to(config.FRONTEND_DIST.resolve()) and candidate.is_file():
        return FileResponse(candidate, headers=_NO_CACHE if candidate.suffix == ".html" else None)
    return FileResponse(index, headers=_NO_CACHE)
