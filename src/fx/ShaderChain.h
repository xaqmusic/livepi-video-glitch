#pragma once

#include <memory>
#include <vector>

#include "ShaderPass.h"
#include "ofFbo.h"
#include "ofRectangle.h"

class ofBaseDraws;

// Runs an ordered list of ShaderPass stages over ping-pong FBOs: pass N's
// output FBO becomes pass N+1's input. ShaderChain doesn't know anything
// about scene semantics -- it just runs whatever passes were added via
// addPass(), in order, every process() call.
class ShaderChain {
public:
    void setup(int width, int height);
    // Re-allocate the ping-pong FBOs to a new render size WITHOUT rebuilding the
    // passes (their shaders are size-independent) -- lets the thermal governor
    // resize a live chain in place, so the layer's clip keeps playing instead of
    // being torn down and re-prerolled. Passes drop render-sized caches via
    // onResize(). setup() is still the one-time build; this is the resize path.
    void resize(int width, int height);
    void addPass(std::unique_ptr<ShaderPass> pass);

    // Draws input through every pass in order; the result is retrievable
    // via getOutputFbo() afterward. Taking an ofBaseDraws (not an ofTexture)
    // lets the video player draw itself into the first FBO, which is what
    // routes planar YUV frames through the renderer's GPU conversion shader
    // -- see ClipPlayer::getDrawable().
    void process(const ofBaseDraws& input, const ControlState& controlState, const LiveParams& liveParams);
    // Same, but seeds the input at an explicit rectangle instead of
    // stretched over the whole FBO -- how layer transforms (contain-fit
    // aspect, scale, x/y position) enter the pipeline. Everything outside
    // the rect is black. rotationDeg spins the source about the rect center
    // (0 = none); it is applied here rather than by the caller because the
    // FBO's begin() resets the modelview.
    void process(const ofBaseDraws& input, const ofRectangle& destRect, const ControlState& controlState,
                 const LiveParams& liveParams, float rotationDeg = 0.0f);
    ofFbo& getOutputFbo();

private:
    std::vector<std::unique_ptr<ShaderPass>> passes;
    ofFbo fboA, fboB;
    bool outputIsA = true;
    bool isSetup = false;
};
