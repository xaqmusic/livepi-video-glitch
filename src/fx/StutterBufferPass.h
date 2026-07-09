#pragma once

#include <string>
#include <vector>

#include "ShaderPass.h"
#include "ofFbo.h"
#include "ofShader.h"

// Beat-repeat stutter (per-layer). Continuously records the layer's frames
// into a timestamped ring; when engaged ("stutter.engage" > 0.5, typically
// mapped to a pad/note for momentary punch-ins), it captures the last
// interval's worth of frames -- the interval set by "stutter.rate"
// (quantized 1/16, 1/8, 1/4, 1/2 note or a full bar against the current
// BPM) -- and loops that window at normal speed until release. Recording
// pauses while engaged so the captured window can't be overwritten;
// release snaps back to the live feed.
//
// The GLSL side (stutter_hold.frag) stays a pure passthrough -- the effect
// is entirely which buffered frame gets fed in as srcTex. Frame lookup is
// by timestamp, so it works identically at 60fps light scenes and ~26fps
// heavy ones.
class StutterBufferPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }

private:
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
