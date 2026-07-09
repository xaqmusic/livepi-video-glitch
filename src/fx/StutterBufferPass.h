#pragma once

#include <array>

#include "ShaderPass.h"
#include "ofFbo.h"
#include "ofShader.h"

// Frame-Dropping & Stutter Buffer (see docs/LivePi VideoGlitcher HLD.pdf):
// keeps a short ring buffer of recently-seen frames so it can freeze on one
// or rapidly loop the last few, instead of a smooth handoff to the live
// video. The GLSL side (stutter_hold.frag) is a pure passthrough -- the
// actual "corrupted digital stream" effect is entirely which buffered frame
// gets fed in as srcTex. The trigger is also gated on knobA (master glitch
// intensity, shared across all three passes) so dialing it down disables
// stutter along with the other two effects.
class StutterBufferPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }

private:
    static constexpr int kRingSize = 4;

    ofShader shader;
    std::string name = "stutter_buffer";
    std::array<ofFbo, kRingSize> ring;
    int writeIndex = 0;
};
