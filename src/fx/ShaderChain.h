#pragma once

#include <memory>
#include <vector>

#include "ShaderPass.h"
#include "ofFbo.h"

// Runs an ordered list of ShaderPass stages over ping-pong FBOs: pass N's
// output FBO becomes pass N+1's input. ShaderChain doesn't know anything
// about scene semantics -- it just runs whatever passes were added via
// addPass(), in order, every process() call.
class ShaderChain {
public:
    void setup(int width, int height);
    void addPass(std::unique_ptr<ShaderPass> pass);

    // Draws inputTexture through every pass in order; the result is
    // retrievable via getOutputFbo() afterward.
    void process(ofTexture& inputTexture, const ControlState& controlState, const Scene& scene);
    ofFbo& getOutputFbo();

private:
    std::vector<std::unique_ptr<ShaderPass>> passes;
    ofFbo fboA, fboB;
    bool outputIsA = true;
};
