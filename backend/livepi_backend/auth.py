"""Shared-password auth behind a signed session cookie -- LAN-only,
single-user gear (docs/videosynth-backend.md's 'sized to match' call).
Every /api route except login depends on require_session; the telemetry
WebSocket checks the same cookie at connect."""

import hmac

from fastapi import APIRouter, Cookie, HTTPException, Response
from itsdangerous import BadSignature, TimestampSigner
from pydantic import BaseModel

from . import config

COOKIE_NAME = "livepi_session"
MAX_AGE_SECS = 30 * 24 * 3600

_signer = TimestampSigner(config.SECRET_KEY)

router = APIRouter()


class LoginBody(BaseModel):
    password: str


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
    if not hmac.compare_digest(body.password, config.PASSWORD):
        raise HTTPException(status_code=401, detail="Wrong password")
    response.set_cookie(
        COOKIE_NAME, _make_token(), max_age=MAX_AGE_SECS, httponly=True, samesite="lax"
    )
    return {"ok": True}


@router.post("/api/logout")
def logout(response: Response):
    response.delete_cookie(COOKIE_NAME)
    return {"ok": True}
