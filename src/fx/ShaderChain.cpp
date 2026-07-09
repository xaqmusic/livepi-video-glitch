#include "ShaderChain.h"

#include "ofGraphics.h"

void ShaderChain::setup(int width, int height) {
    ofFboSettings settings;
    settings.width = width;
    settings.height = height;
    settings.internalformat = GL_RGBA;
    fboA.allocate(settings);
    fboB.allocate(settings);

    isSetup = true;
    for (auto& pass : passes) pass->setup();
}

void ShaderChain::addPass(std::unique_ptr<ShaderPass> pass) {
    // Passes register in whichever order the caller finds natural relative
    // to setup() -- a pass added to an already-set-up chain gets its
    // setup() (shader build) immediately, instead of silently drawing with
    // program 0 forever (which on the desktop driver renders the texcoord
    // ramp instead of the effect -- found the hard way).
    if (isSetup) pass->setup();
    passes.push_back(std::move(pass));
}

void ShaderChain::process(const ofBaseDraws& input, const ControlState& controlState, const Scene& scene) {
    // Seed fboA with the raw input frame so the first pass has something to
    // read regardless of how many passes are configured (including zero).
    fboA.begin();
    ofClear(0, 0, 0, 255);
    ofSetColor(255);  // the video shaders multiply by globalColor
    input.draw(0, 0, fboA.getWidth(), fboA.getHeight());
    fboA.end();
    outputIsA = true;

    for (auto& pass : passes) {
        ofFbo& src = outputIsA ? fboA : fboB;
        ofFbo& dst = outputIsA ? fboB : fboA;
        pass->apply(src, dst, controlState, scene);
        outputIsA = !outputIsA;
    }
}

ofFbo& ShaderChain::getOutputFbo() {
    return outputIsA ? fboA : fboB;
}
