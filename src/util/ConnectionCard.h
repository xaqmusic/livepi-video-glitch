#pragma once

#include <string>

#include "ofFbo.h"

// The on-screen setup overlay the appliance paints onto whatever display is
// attached, so a new owner can connect with zero technical steps: it shows the
// control-network Wi-Fi name, the printed device code (which is also the AP
// key AND the web-UI/SSH password -- all one secret), and the URL to open.
// Plug the box into a screen -> everything needed to connect is right there.
//
// Data comes from the box itself: the hostname (livepi-XXXX -> AP LivePi-XXXX,
// matching firstboot.sh), the code from $LIVEPI_DATA_DIR/backend/.env, and live
// IPs from NetInfo. Text renders into an FBO at 1x then upscales nearest-
// filtered, so it's big and crisp without shipping a TrueType font.
class ConnectionCard {
public:
    // Setup   = join the box's hotspot (shown on a fresh/unclaimed box).
    // Lan      = the box just joined a venue Wi-Fi and the hotspot dropped, so
    //            tell the user how to reach it on the LAN instead (the phone
    //            that was on the hotspot is now stranded). Same panel + QR
    //            machinery, different content.
    enum class Mode { Setup, Lan };

    // Read the static bits once (hostname, derived AP SSID, device code).
    void gather();

    // Overlay the card onto the current frame, centered.
    void draw(int screenW, int screenH, Mode mode = Mode::Setup);

private:
    std::string hostname;
    std::string apSsid;
    std::string code;

    ofFbo panel;
    std::string builtContent;  // cache: only re-render the FBO when text changes
};
