#include "ConnectionCard.h"

#include <unistd.h>  // gethostname

#include <algorithm>
#include <fstream>
#include <vector>

#include "ofGraphics.h"
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
constexpr int kPad = 14;

}  // namespace

void ConnectionCard::gather() {
    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf) - 1) == 0) hostname = buf;
    if (hostname.empty()) hostname = "livepi";
    apSsid = deriveApSsid(hostname);
    code = readDeviceCode();
}

void ConnectionCard::draw(int screenW, int screenH) {
    // --- build the card's text (live IPs included) ---------------------------
    std::vector<std::string> lines;
    lines.push_back("CONNECT TO LIVEPI");
    lines.push_back("");
    lines.push_back("Wi-Fi network   " + apSsid);
    lines.push_back("Password        " + (code.empty() ? std::string("(set on first boot)") : code));
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
    lines.push_back("Join the Wi-Fi and the setup page opens by itself.");

    std::string content;
    for (const auto& l : lines) content += l + "\n";

    // --- (re)render the panel FBO only when the text changed -----------------
    size_t maxLen = 0;
    for (const auto& l : lines) maxLen = std::max(maxLen, l.size());
    int cw = static_cast<int>(maxLen) * kCharW + kPad * 2;
    int ch = static_cast<int>(lines.size()) * kLineH + kPad * 2;

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
            // Transparent highlight background (we draw our own panel); the
            // Highlight overload avoids the plain ofDrawBitmapString ->
            // ofToString<string> link path. +11: baseline within the line box.
            ofDrawBitmapStringHighlight(lines[i], kPad, kPad + static_cast<int>(i) * kLineH + 11,
                                        ofColor(0, 0, 0, 0), fg);
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
