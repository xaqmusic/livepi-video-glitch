# SPDX-License-Identifier: MIT
# Copyright (c) 2026 xaqmusic
"""Captive-portal probe responders.

When a phone/laptop joins the box's AP, its OS fetches a well-known URL to
check for internet. We make that check "fail" so the OS pops its "Sign in to
network" browser straight at the LivePi UI (docs/distribution.md "On-screen
help"). Two pieces outside this file make the probes land here:

  - config/networkmanager/dnsmasq-shared.d/livepi-captive.conf resolves the
    OS probe domains (captive.apple.com, connectivitycheck.gstatic.com, ...)
    to the AP gateway. Only those dedicated domains are hijacked, so AP
    clients keep real internet for everything else.
  - config/livepi-captive.nft redirects :80 on the AP to the backend's :8080.

Both are inert unless the AP is up (the dnsmasq config only applies to NM's
shared-mode instance; the nft rule only matches the AP subnet), so no
setup-mode flag is needed. These routes are necessarily unauthenticated --
the joining client isn't logged in yet -- and are registered before the SPA
catch-all in main.py so they win over it.
"""

from fastapi import APIRouter
from fastapi.responses import RedirectResponse

router = APIRouter()

# Where the captive browser lands: the app root, relative so it stays on
# whatever host/port the client reached us at. A fresh box's React app routes
# on from here to the login screen.
_PORTAL_URL = "/"

# The path each OS fetches for its connectivity check. Returning a redirect
# instead of the expected 204/"Success" body is what flags "captive portal".
_PROBE_PATHS = (
    "/generate_204",              # Android / ChromeOS
    "/gen_204",                   # Android (alt)
    "/hotspot-detect.html",       # iOS / macOS
    "/library/test/success.html",  # iOS / macOS (alt)
    "/connecttest.txt",           # Windows
    "/ncsi.txt",                  # Windows (legacy)
    "/canonical.html",            # Firefox / GNOME
    "/success.txt",               # misc
    "/redirect",                  # misc
)


def _to_portal() -> RedirectResponse:
    return RedirectResponse(_PORTAL_URL, status_code=302)


for _path in _PROBE_PATHS:
    router.add_api_route(_path, _to_portal, methods=["GET"], include_in_schema=False)
