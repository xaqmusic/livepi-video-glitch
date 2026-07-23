#pragma once

#include <string>
#include <vector>

#include "ShaderPass.h"
#include "ofFbo.h"
#include "ofShader.h"

// Beat-repeat stutter (per-layer). Records ONLY while engaged, so an idle
// stutter costs nothing: when "stutter.engage" crosses > 0.5 (typically a
// pad/note momentary punch-in) it captures one interval's worth of frames
// FORWARD from that moment -- the interval set by "stutter.rate" (quantized
// 1/16, 1/8, 1/4, 1/2 note or a full bar against the current BPM) -- passing
// the live feed through meanwhile, then loops that captured window at normal
// speed until release, when it snaps back to live.
//
// This deliberately does NOT record while idle. The earlier design kept a
// continuous ring so an engage could loop the interval BEFORE the press
// (retroactive), but that meant allocating kRingCapacity full-screen FBOs
// (~350MB at 1080-ish/wide panels) and copying the whole frame every frame
// on EVERY layer even when stutter was never used -- a large, permanent GPU
// tax for a momentary effect. Forward-capture loops the beat you punch ON
// (musically the common case) for zero idle cost. If retroactive is ever
// wanted back, do it with a small low-res ring, not a full-res one.
//
// The GLSL side (stutter_hold.frag) stays a pure passthrough -- the effect
// is entirely which buffered frame gets fed in as srcTex.
class StutterBufferPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }
    // On a render resize the ring holds frames at the old size; drop them so
    // slots re-allocate at the new size (a resize discards stutter history --
    // fine, it's a momentary effect).
    void onResize() override;

private:
    // Copy the live frame into the next ring slot (lazy-allocated). Only ever
    // called while capturing an engaged window -- never idle.
    void recordFrame(ofFbo& src, double now);

    // 64 slots ~= 1-2.5s of history depending on frame rate -- enough for a
    // full bar at typical tempos. Slots allocate lazily (one per frame as
    // the ring first fills), so startup pays no allocation burst.
    static constexpr int kRingCapacity = 64;

    struct Slot {
        ofFbo fbo;
        double timeSeconds = -1.0;  // -1 = never written
    };

    ofShader shader;
    std::string name = "stutter_buffer";
    std::vector<Slot> ring{kRingCapacity};
    int writeIndex = 0;

    // Capturing the forward window right after an engage (recording live
    // frames until we have one interval, then flipping to looping).
    bool capturing = false;
    double captureStartSecs = 0.0;

    bool engaged = false;
    // The captured window, oldest-first, played back by INDEX one frame
    // per render and wrapping -- not by wall-clock lookup. Clock-based
    // nearest-neighbor resampling aliases badly when the interval is a
    // fractional number of render frames (a 1/16 note at ~28fps is 3.5
    // frames: alternate cycles land on different frames and the visible
    // pattern only repeats every TWO cycles -- "1/16 looks like 1/8").
    std::vector<int> loopSlots;
    size_t playIndex = 0;
};
