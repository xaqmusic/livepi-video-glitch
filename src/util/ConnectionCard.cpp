#include "ConnectionCard.h"

#include <unistd.h>  // gethostname

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

#include "ofGraphics.h"
#include "third_party/qrcodegen/qrcodegen.hpp"
#include "util/DataPath.h"
#include "util/NetInfo.h"

namespace {

// Pull LIVEPI_PASSWORD out of the per-device env file firstboot wrote. Empty
// (missing/unprovisioned) is handled by the caller.
std::string readDeviceCode() {
    std::ifstream f(livepi::userDataPath("backend/.env"));
    std::string line;
    const std::string key = "LIVEPI_PASSWORD=";
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) == 0) return line.substr(key.size());
    }
    return "";
}

// hostname "livepi-XXXX" -> AP "LivePi-XXXX" (firstboot.sh's naming).
std::string deriveApSsid(const std::string& host) {
    const std::string prefix = "livepi-";
    if (host.rfind(prefix, 0) == 0) return "LivePi-" + host.substr(prefix.size());
    return "LivePi";
}

constexpr int kCharW = 8;   // oF bitmap glyph advance
constexpr int kLineH = 15;
constexpr int kPad = 16;
constexpr int kGap = 28;    // between the text column and the QR
constexpr int kQuiet = 4;   // QR quiet-zone in modules

// Draw a QR of `text` into the current FBO: a light card with a quiet zone,
// dark modules on top -- scannable dark-on-light regardless of the panel's
// dark background. Returns the side length actually drawn (px).
float drawQr(const std::string& text, float x, float y, float targetPx) {
    using qrcodegen::QrCode;
    QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::MEDIUM);
    int n = qr.getSize();
    float mod = std::floor(targetPx / (n + 2 * kQuiet));
    if (mod < 1.0f) mod = 1.0f;
    float full = (n + 2 * kQuiet) * mod;

    ofFill();
    ofSetColor(238, 238, 240);
    ofDrawRectangle(x, y, full, full);
    ofSetColor(10, 10, 12);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            if (qr.getModule(i, j)) {
                ofDrawRectangle(x + (i + kQuiet) * mod, y + (j + kQuiet) * mod, mod, mod);
            }
        }
    }
    return full;
}

// WPA join payload phones recognise from a QR (offers "join this network").
// Our SSID/code are [A-Za-z0-9-] only, so no metacharacter escaping needed.
std::string wifiJoinPayload(const std::string& ssid, const std::string& code) {
    return "WIFI:T:WPA;S:" + ssid + ";P:" + code + ";;";
}

}  // namespace

void ConnectionCard::gather() {
    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf) - 1) == 0) hostname = buf;
    if (hostname.empty()) hostname = "livepi";
    apSsid = deriveApSsid(hostname);
    code = readDeviceCode();
}

void ConnectionCard::draw(int screenW, int screenH, Mode mode) {
    // --- text content + QR payload/caption (mode-specific) -------------------
    std::vector<std::string> lines;
    std::string qrPayload;   // empty -> no QR
    std::string qrCaption;

    // Once the owner sets a custom web password (auth.json on DATA_DIR), the
    // printed factory code no longer logs into the UI -- only the AP key and SSH
    // still use it -- so showing it as "the password" is misleading. Point at the
    // 30s-button reset instead. The join QR still carries the (unchanged) factory
    // AP key, so joining the hotspot is unaffected. Checked live (not cached in
    // gather()) so a UI password change takes effect on the next card show.
    const bool ownerPasswordSet = std::ifstream(livepi::userDataPath("auth.json")).good();

    if (mode == Mode::Lan) {
        // The box just joined a venue Wi-Fi; the setup hotspot has dropped, so
        // the phone that was on it is stranded. Point the user at the box's LAN
        // address (mDNS name + client IP) with a QR to open once they're back on
        // their own Wi-Fi. The IP arrives a beat after the join (DHCP), and the
        // content cache re-renders when it does.
        std::string lanIp;
        for (const auto& i : NetInfo::interfaces()) {
            if (!i.isAp) {
                lanIp = i.ip;
                break;
            }
        }
        const std::string mdns = "http://" + hostname + ".local:8080";
        const std::string ipUrl = lanIp.empty() ? std::string() : ("http://" + lanIp + ":8080");
        lines.push_back("LIVEPI IS ON YOUR WI-FI");
        lines.push_back("");
        lines.push_back("The setup hotspot has closed. Reconnect this");
        lines.push_back("phone or laptop to your own Wi-Fi network,");
        lines.push_back("then open the box at:");
        lines.push_back("");
        lines.push_back("  " + mdns);
        if (!ipUrl.empty()) lines.push_back("  " + ipUrl);
        lines.push_back("");
        // Re-show the login password (same one from the setup card): the hotspot
        // card that first displayed it is gone, and the long generated string is
        // easy to forget. Both cards retire for good after the first login, so
        // this is only ever on-screen during brand-new-box setup -- with a nudge
        // to change it so it isn't left readable on a projector.
        if (ownerPasswordSet) {
            lines.push_back("Log in with the password you set.");
            lines.push_back("Forgot it? Hold the box button 30s to reset");
            lines.push_back("the login to the printed code.");
        } else if (!code.empty()) {
            lines.push_back("Password        " + code);
            lines.push_back("Change it once you sign in (Settings > Password)");
            lines.push_back("so it isn't left showing on this screen.");
        }
        lines.push_back("");
        lines.push_back(lanIp.empty() ? "Getting an address -- this updates on its own..."
                                      : "Scan the code once you're on the same Wi-Fi.");
        // QR the client-IP URL (most reliably scannable on the subnet); fall
        // back to the mDNS name until an IP lands.
        qrPayload = ipUrl.empty() ? mdns : ipUrl;
        qrCaption = "scan to open";
    } else {
        const bool haveCode = !code.empty();
        lines.push_back("CONNECT TO LIVEPI");
        lines.push_back("");
        lines.push_back("Wi-Fi network   " + apSsid);
        lines.push_back("Password        " + (haveCode ? code : std::string("(set on first boot)")));
        lines.push_back("");
        lines.push_back("Then open       http://" + hostname + ".local:8080");
        lines.push_back("  on hotspot    http://10.42.0.1:8080");

        auto ifaces = NetInfo::interfaces();
        if (!ifaces.empty()) {
            std::string ipLine = "This box        ";
            for (size_t i = 0; i < ifaces.size(); i++) {
                ipLine += ifaces[i].name + " " + ifaces[i].ip + (ifaces[i].isAp ? " (hotspot)" : "");
                if (i + 1 < ifaces.size()) ipLine += "   ";
            }
            lines.push_back(ipLine);
        }
        lines.push_back("");
        lines.push_back(haveCode ? "Scan the code to join, or enter it by hand."
                                 : "Waiting for first-boot setup...");
        if (haveCode) {
            qrPayload = wifiJoinPayload(apSsid, code);
            qrCaption = "scan to join Wi-Fi";
        }
    }

    const bool hasQr = !qrPayload.empty();

    // Cache key: re-render the FBO only when anything visible changed.
    std::string content;
    for (const auto& l : lines) content += l + "\n";
    content += "|qr:" + qrPayload;

    // --- geometry ------------------------------------------------------------
    size_t maxLen = 0;
    for (const auto& l : lines) maxLen = std::max(maxLen, l.size());
    int textW = static_cast<int>(maxLen) * kCharW;
    int textH = static_cast<int>(lines.size()) * kLineH;

    // QR sized to the text column's height (with a caption line beneath).
    int qrBlock = 0;  // full QR side once drawn
    int cw, ch;
    if (hasQr) {
        qrBlock = textH;  // approximate; drawQr floors modules to fit
        cw = kPad + textW + kGap + qrBlock + kPad;
        ch = kPad + std::max(textH, qrBlock + kLineH) + kPad;
    } else {
        cw = kPad + textW + kPad;
        ch = kPad + textH + kPad;
    }

    if (!panel.isAllocated() || panel.getWidth() != cw || panel.getHeight() != ch) {
        panel.allocate(cw, ch, GL_RGBA);
        panel.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        builtContent.clear();
    }
    if (content != builtContent) {
        builtContent = content;
        panel.begin();
        ofClear(0, 0, 0, 0);
        ofFill();
        ofSetColor(12, 12, 18, 240);
        ofDrawRectangle(0, 0, cw, ch);
        ofSetColor(0, 210, 255);
        ofNoFill();
        ofDrawRectangle(1, 1, cw - 2, ch - 2);
        ofFill();

        for (size_t i = 0; i < lines.size(); i++) {
            if (lines[i].empty()) continue;
            ofColor fg = i == 0                              ? ofColor(0, 210, 255)     // title accent
                         : lines[i].rfind("Password", 0) == 0 ? ofColor(120, 255, 150)  // code stands out
                                                              : ofColor(225, 225, 232);
            // Transparent highlight bg (we draw our own panel); the Highlight
            // overload avoids the ofDrawBitmapString -> ofToString<string> link
            // path. +11: baseline within the line box.
            ofDrawBitmapStringHighlight(lines[i], kPad, kPad + static_cast<int>(i) * kLineH + 11,
                                        ofColor(0, 0, 0, 0), fg);
        }

        if (hasQr) {
            float qx = cw - kPad - qrBlock;
            float qy = kPad;
            float drawn = drawQr(qrPayload, qx, qy, qrBlock);
            ofSetColor(180, 210, 230);
            ofDrawBitmapStringHighlight(qrCaption, static_cast<int>(qx),
                                        static_cast<int>(qy + drawn) + 12, ofColor(0, 0, 0, 0),
                                        ofColor(180, 210, 230));
        }
        panel.end();
    }

    // --- overlay: dim the frame, then draw the panel scaled + centered -------
    float scale = std::min({screenW * 0.9f / cw, screenH * 0.85f / ch, 4.0f});
    scale = std::max(scale, 1.0f);
    float dw = cw * scale;
    float dh = ch * scale;

    ofPushStyle();
    ofEnableAlphaBlending();
    ofSetColor(0, 0, 0, 120);  // knock the visuals back so the card reads
    ofDrawRectangle(0, 0, screenW, screenH);
    ofSetColor(255);
    panel.draw((screenW - dw) * 0.5f, (screenH - dh) * 0.5f, dw, dh);
    ofPopStyle();
}
