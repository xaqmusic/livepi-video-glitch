"""App factory: API routers + the built frontend served as static files
with an SPA fallback (any non-API path returns index.html so React Router
owns /edit, /live, etc.)."""

from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from . import auth, clips, commands, config, effects, shows, storage, telemetry


def _seed_data() -> None:
    """A fresh Pi (deploys exclude show data -- it's Pi-authored) still
    boots to something: an empty default show and an empty clip registry."""
    config.SHOWS_DIR.mkdir(parents=True, exist_ok=True)
    if storage.get_active_show_name() is None:
        if not (config.SHOWS_DIR / "default.json").exists():
            storage.atomic_write_json(
                config.SHOWS_DIR / "default.json", {"schemaVersion": 1, "scenes": []}
            )
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


@app.get("/api/health")
def health():
    return {"ok": True, "dataDir": str(config.DATA_DIR)}


if config.FRONTEND_DIST.is_dir():
    app.mount("/assets", StaticFiles(directory=config.FRONTEND_DIST / "assets"), name="assets")

    @app.get("/{path:path}")
    def spa(path: str):
        candidate = config.FRONTEND_DIST / path
        if path and candidate.is_file():
            return FileResponse(candidate)
        return FileResponse(config.FRONTEND_DIST / "index.html")
else:

    @app.get("/")
    def no_frontend():
        return JSONResponse(
            {"message": "Frontend not built -- API docs at /docs. Run `npm run build` in frontend/."}
        )
