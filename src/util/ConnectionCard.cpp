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

void ConnectionCard::draw(int screenW, int screenH) {
    // --- text content (live IPs included) ------------------------------------
    const bool haveCode = !code.empty();
    std::vector<std::string> lines;
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
    lines.push_back(haveCode ? "Scan the code to join, or enter it by hand." : "Waiting for first-boot setup...");

    // Cache key: re-render the FBO only when anything visible changed.
    std::string content;
    for (const auto& l : lines) content += l + "\n";
    content += "|qr:" + (haveCode ? wifiJoinPayload(apSsid, code) : std::string());

    // --- geometry ------------------------------------------------------------
    size_t maxLen = 0;
    for (const auto& l : lines) maxLen = std::max(maxLen, l.size());
    int textW = static_cast<int>(maxLen) * kCharW;
    int textH = static_cast<int>(lines.size()) * kLineH;

    // QR sized to the text column's height (with a caption line beneath).
    int qrTarget = haveCode ? textH : 0;
    int qrBlock = 0;  // full QR side once drawn
    int cw, ch;
    if (haveCode) {
        // Approximate QR side for layout; drawQr floors modules to fit.
        qrBlock = qrTarget;
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

        if (haveCode) {
            float qx = cw - kPad - qrBlock;
            float qy = kPad;
            float drawn = drawQr(wifiJoinPayload(apSsid, code), qx, qy, qrBlock);
            ofSetColor(180, 210, 230);
            ofDrawBitmapStringHighlight("scan to join Wi-Fi", static_cast<int>(qx),
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
