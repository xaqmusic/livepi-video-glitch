#pragma once

#include <memory>
#include <vector>

#include "ShaderPass.h"
#include "ofFbo.h"

class ofBaseDraws;

// Runs an ordered list of ShaderPass stages over ping-pong FBOs: pass N's
// output FBO becomes pass N+1's input. ShaderChain doesn't know anything
// about scene semantics -- it just runs whatever passes were added via
// addPass(), in order, every process() call.
class ShaderChain {
public:
    void setup(int width, int height);
    void addPass(std::unique_ptr<ShaderPass> pass);

    // Draws input through every pass in order; the result is retrievable
    // via getOutputFbo() afterward. Taking an ofBaseDraws (not an ofTexture)
    // lets the video player draw itself into the first FBO, which is what
    // routes planar YUV frames through the renderer's GPU conversion shader
    // -- see ClipPlayer::getDrawable().
    void process(const ofBaseDraws& input, const ControlState& controlState, const Scene& scene);
    ofFbo& getOutputFbo();

private:
    std::vector<std::unique_ptr<ShaderPass>> passes;
    ofFbo fboA, fboB;
    bool outputIsA = true;
    bool isSetup = false;
};
