#pragma once

#include "ShaderPass.h"
#include "ofShader.h"

// Chromatic Aberration & Color Bleed (see docs/LivePi VideoGlitcher HLD.pdf):
// splits the RGB channels apart, separation driven by a blend of live audio
// level and the scene's configured intensity. GLSL side:
// bin/data/shaders/chromatic_aberration.frag.
class ChromaticAberrationPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const Scene& scene) override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "chromatic_aberration";
};
