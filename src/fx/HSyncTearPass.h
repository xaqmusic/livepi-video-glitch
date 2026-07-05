#pragma once

#include "ShaderPass.h"
#include "ofShader.h"

// Horizontal Sync Displacement (see docs/LivePi VideoGlitcher HLD.pdf):
// shifts scanlines sideways by an amount driven by a noise field, spiking
// hard on each beat and decaying until the next one -- what turns a steady
// wobble into a "violent snap sideways." GLSL side:
// bin/data/shaders/hsync_tear.frag.
class HSyncTearPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const Scene& scene) override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "hsync_tear";
    float noisePhase = 0.0f;
    int lastBeatSeen = -1;
    float beatSpike = 0.0f;
};
