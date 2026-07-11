"""WiFi provisioning over NetworkManager, driven from the web UI -- the box
is a standing control-network AP by default, and this lets a phone put it
onto a venue network without a terminal (docs/distribution.md "Networking &
provisioning").

We shell out to `nmcli` rather than pull in a D-Bus dependency: the commands
are few and stable. The backend runs headless as a systemd service, so it
needs the polkit rule from scripts/install-network-perms.sh to be allowed to
drive NM at all (without it every call returns "not authorized").

Empirically confirmed on the Pi 4's brcmfmac radio: it scans for venue
networks even while our own AP is up, so the scan list is live during
provisioning -- no need to tear the AP down first.
"""

import shutil
import socket
import subprocess

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from .auth import require_session

router = APIRouter(dependencies=[Depends(require_session)])

# The standing control-network AP's NM connection name (created by firstboot /
# scripts; see the AP profile work). connect/forget try to restore it so a
# failed venue join never strands the operator without the control network.
AP_CON_NAME = "livepi-ap"


class ConnectBody(BaseModel):
    ssid: str
    password: str = ""
    hidden: bool = False


class ForgetBody(BaseModel):
    ssid: str


def _have_nmcli() -> bool:
    return shutil.which("nmcli") is not None


def _nmcli(args: list[str], timeout: float = 10.0) -> subprocess.CompletedProcess:
    """Run nmcli, capturing output. Raises 503 if nmcli is missing (e.g. a
    desktop dev box) or wedges; individual callers decide what a non-zero
    return means."""
    if not _have_nmcli():
        raise HTTPException(status_code=503, detail="NetworkManager (nmcli) not available on this host")
    try:
        return subprocess.run(
            ["nmcli", *args], capture_output=True, text=True, timeout=timeout
        )
    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=504, detail=f"nmcli timed out: {' '.join(args)}")


def _field_value(line: str) -> str:
    """Value half of a terse `FIELD:value` line (`IP4.ADDRESS[1]:10.0.0.5/24`)."""
    return line.split(":", 1)[1] if ":" in line else ""


def _device_ip(dev: str) -> str | None:
    r = _nmcli(["-t", "-f", "IP4.ADDRESS", "device", "show", dev])
    for line in r.stdout.splitlines():
        if line.startswith("IP4.ADDRESS"):
            addr = _field_value(line).split("/")[0]
            return addr or None
    return None


def _conn_field(conn: str, field: str) -> str | None:
    r = _nmcli(["-t", "-f", field, "connection", "show", conn])
    for line in r.stdout.splitlines():
        if line.startswith(field):
            return _field_value(line) or None
    return None


@router.get("/api/network/status")
def status():
    """Current reachability: ethernet, our own AP, and any venue-WiFi client
    link, plus whether there's a real internet uplink."""
    result = {
        "hostname": socket.gethostname(),
        "ethernet": {"connected": False, "ip": None},
        "ap": {"active": False, "ssid": None, "ip": None},
        "wifiClient": {"connected": False, "ssid": None, "ip": None},
        "online": False,
    }

    devices: dict[str, dict] = {}
    r = _nmcli(["-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"])
    for line in r.stdout.splitlines():
        parts = line.split(":", 3)
        if len(parts) == 4:
            dev, typ, state, conn = parts
            devices[dev] = {"type": typ, "state": state, "connection": conn}

    eth = next((d for d, i in devices.items() if i["type"] == "ethernet"), None)
    if eth and devices[eth]["state"] == "connected":
        result["ethernet"] = {"connected": True, "ip": _device_ip(eth)}

    wlan = next((d for d, i in devices.items() if i["type"] == "wifi"), None)
    if wlan and devices[wlan]["state"] == "connected":
        conn = devices[wlan]["connection"]
        if conn and conn != "--":
            mode = _conn_field(conn, "802-11-wireless.mode")
            ssid = _conn_field(conn, "802-11-wireless.ssid") or conn
            ip = _device_ip(wlan)
            if mode == "ap":
                result["ap"] = {"active": True, "ssid": ssid, "ip": ip}
            else:
                result["wifiClient"] = {"connected": True, "ssid": ssid, "ip": ip}

    conn_status = _nmcli(["-t", "-f", "CONNECTIVITY", "general", "status"])
    result["online"] = conn_status.stdout.strip() == "full"
    return result


@router.get("/api/network/scan")
def scan():
    """Nearby venue networks (strongest per SSID), for the provisioning UI.
    Our own AP and hidden/blank SSIDs are dropped."""
    _nmcli(["device", "wifi", "rescan"], timeout=20)  # best-effort; nmcli rate-limits
    r = _nmcli(["-t", "-f", "IN-USE,SIGNAL,SECURITY,SSID", "device", "wifi", "list"], timeout=10)

    own_ap = _conn_field(AP_CON_NAME, "802-11-wireless.ssid")
    best: dict[str, dict] = {}
    for line in r.stdout.splitlines():
        # SSID is last so a colon inside it can't shift the earlier fields.
        parts = line.split(":", 3)
        if len(parts) != 4:
            continue
        in_use, signal, security, ssid = parts
        ssid = ssid.replace("\\:", ":")
        if not ssid or ssid == own_ap:
            continue
        try:
            sig = int(signal)
        except ValueError:
            sig = 0
        prev = best.get(ssid)
        if prev is None or sig > prev["signal"]:
            best[ssid] = {
                "ssid": ssid,
                "signal": sig,
                "security": security or "open",
                "inUse": in_use.strip() == "*",
            }
    return {"networks": sorted(best.values(), key=lambda n: n["signal"], reverse=True)}


def _ensure_ap() -> None:
    """Best-effort: bring the standing control AP back so a failed/forgotten
    venue join never leaves the box unreachable. The autohotspot dispatcher is
    the durable safety net; this just avoids a gap in the meantime."""
    if _conn_field(AP_CON_NAME, "connection.id"):
        _nmcli(["connection", "up", AP_CON_NAME], timeout=25)


@router.post("/api/network/connect")
def connect(body: ConnectBody):
    """Join a venue network as a client. On a single radio this drops the AP
    (the intended mode-switch); if it fails we bring the AP back up."""
    ssid = body.ssid.strip()
    if not ssid:
        raise HTTPException(status_code=422, detail="SSID is required")
    args = ["device", "wifi", "connect", ssid]
    if body.password:
        args += ["password", body.password]
    if body.hidden:
        args += ["hidden", "yes"]
    r = _nmcli(args, timeout=50)
    if r.returncode != 0:
        _ensure_ap()
        detail = r.stderr.strip() or "Could not connect"
        raise HTTPException(status_code=400, detail=detail)
    return {"ok": True, "ssid": ssid}


@router.post("/api/network/forget")
def forget(body: ForgetBody):
    """Drop a saved venue network and fall back to the control AP."""
    ssid = body.ssid.strip()
    if ssid:
        # nmcli names the client profile after the SSID it joined.
        _nmcli(["connection", "delete", ssid], timeout=15)
    _ensure_ap()
    return {"ok": True}
