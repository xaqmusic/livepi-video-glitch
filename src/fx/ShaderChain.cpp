#include "ShaderChain.h"

#include "ofGraphics.h"

void ShaderChain::setup(int width, int height) {
    ofFboSettings settings;
    settings.width = width;
    settings.height = height;
    settings.internalformat = GL_RGBA;
    fboA.allocate(settings);
    fboB.allocate(settings);

    for (auto& pass : passes) pass->setup();
}

void ShaderChain::addPass(std::unique_ptr<ShaderPass> pass) {
    passes.push_back(std::move(pass));
}

void ShaderChain::process(ofTexture& inputTexture, const ControlState& controlState, const Scene& scene) {
    // Seed fboA with the raw input frame so the first pass has something to
    // read regardless of how many passes are configured (including zero).
    fboA.begin();
    ofClear(0, 0, 0, 255);
    inputTexture.draw(0, 0, fboA.getWidth(), fboA.getHeight());
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
