"""Shared-password auth behind a signed session cookie -- LAN-only,
single-user gear (docs/videosynth-backend.md's 'sized to match' call).
Every /api route except login depends on require_session; the telemetry
WebSocket checks the same cookie at connect."""

import hashlib
import hmac
import secrets

from fastapi import APIRouter, Cookie, Depends, HTTPException, Response
from itsdangerous import BadSignature, TimestampSigner
from pydantic import BaseModel

from . import config, storage

COOKIE_NAME = "livepi_session"
MAX_AGE_SECS = 30 * 24 * 3600

# A UI-set password lives here (salted hash, set via POST
# /api/auth/password) and overrides the LIVEPI_PASSWORD env default.
# Inside DATA_DIR so it's Pi-authored state: .rsyncfilter shields it from
# deploys, same as shows and the clip registry.
AUTH_PATH = config.DATA_DIR / "auth.json"

_signer = TimestampSigner(config.SECRET_KEY)

router = APIRouter()


class LoginBody(BaseModel):
    password: str


class ChangePasswordBody(BaseModel):
    current: str
    new: str


def _hash(password: str, salt: str) -> str:
    return hashlib.sha256((salt + password).encode()).hexdigest()


def _password_matches(candidate: str) -> bool:
    """Check against the UI-set password if one exists, else the env one."""
    stored = storage.read_json(AUTH_PATH) if AUTH_PATH.is_file() else None
    if stored and "passwordHash" in stored and "salt" in stored:
        return hmac.compare_digest(_hash(candidate, stored["salt"]), stored["passwordHash"])
    return hmac.compare_digest(candidate, config.PASSWORD)


def _make_token() -> str:
    return _signer.sign(b"ok").decode()


def is_valid_token(token: str | None) -> bool:
    if not token:
        return False
    try:
        _signer.unsign(token, max_age=MAX_AGE_SECS)
        return True
    except BadSignature:
        return False


def require_session(livepi_session: str | None = Cookie(default=None)) -> None:
    if not is_valid_token(livepi_session):
        raise HTTPException(status_code=401, detail="Not logged in")


@router.post("/api/login")
def login(body: LoginBody, response: Response):
    if not _password_matches(body.password):
        raise HTTPException(status_code=401, detail="Wrong password")
    response.set_cookie(
        COOKIE_NAME, _make_token(), max_age=MAX_AGE_SECS, httponly=True, samesite="lax"
    )
    return {"ok": True}


@router.post("/api/auth/password", dependencies=[Depends(require_session)])
def change_password(body: ChangePasswordBody):
    if not _password_matches(body.current):
        raise HTTPException(status_code=401, detail="Current password is wrong")
    new = body.new.strip()
    if len(new) < 4:
        raise HTTPException(status_code=422, detail="New password must be at least 4 characters")
    salt = secrets.token_hex(16)
    storage.atomic_write_json(AUTH_PATH, {"passwordHash": _hash(new, salt), "salt": salt})
    # The signed session cookie stays valid -- single-user LAN gear; a
    # password change shouldn't log the changer out of their own session.
    return {"ok": True}


@router.post("/api/logout")
def logout(response: Response):
    response.delete_cookie(COOKIE_NAME)
    return {"ok": True}
