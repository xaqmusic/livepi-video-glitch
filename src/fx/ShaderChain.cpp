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

void ShaderChain::resize(int width, int height) {
    ofFboSettings settings;
    settings.width = width;
    settings.height = height;
    settings.internalformat = GL_RGBA;
    fboA.allocate(settings);   // realloc clears them; the next process() repaints
    fboB.allocate(settings);
    outputIsA = true;
    for (auto& pass : passes) pass->onResize();  // drop render-sized caches
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

void ShaderChain::process(const ofBaseDraws& input, const ControlState& controlState, const LiveParams& liveParams) {
    process(input, ofRectangle(0, 0, fboA.getWidth(), fboA.getHeight()), controlState, liveParams);
}

void ShaderChain::process(const ofBaseDraws& input, const ofRectangle& destRect, const ControlState& controlState,
                          const LiveParams& liveParams, float rotationDeg) {
    // Seed fboA with the raw input frame so the first pass has something to
    // read regardless of how many passes are configured (including zero).
    fboA.begin();
    // An alpha-carrying source (RGBA PNG) is written VERBATIM, blending off:
    // an alpha blend against the opaque black clear would fold its transparent
    // pixels down to opaque black (a = a*a + 1-a, which is 1 at a = 0) and the
    // compositor would draw a black box over the layers beneath. Same idiom the
    // transparent generator passes use. Opaque sources keep the opaque seed.
    ofClear(0, 0, 0, preserveSourceAlpha ? 0 : 255);
    if (preserveSourceAlpha) ofEnableBlendMode(OF_BLENDMODE_DISABLED);
    ofSetColor(255);  // the video shaders multiply by globalColor
    // Layer rotation spins the source about the dest-rect center. Applied
    // HERE, inside fboA.begin(): the FBO resets the modelview, so a matrix
    // pushed by the caller before process() would be discarded. The rect
    // center is invariant under the flip trick (negative width/height from the
    // opposite edge), so rotation composes correctly with flips.
    if (rotationDeg != 0.0f) {
        float rcx = destRect.x + destRect.width * 0.5f;
        float rcy = destRect.y + destRect.height * 0.5f;
        ofPushMatrix();
        ofTranslate(rcx, rcy);
        ofRotateDeg(rotationDeg, 0.0f, 0.0f, 1.0f);
        ofTranslate(-rcx, -rcy);
        input.draw(destRect.x, destRect.y, destRect.width, destRect.height);
        ofPopMatrix();
    } else {
        input.draw(destRect.x, destRect.y, destRect.width, destRect.height);
    }
    if (preserveSourceAlpha) ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    fboA.end();
    outputIsA = true;

    for (auto& pass : passes) {
        if (!pass->isActive(liveParams)) continue;
        ofFbo& src = outputIsA ? fboA : fboB;
        ofFbo& dst = outputIsA ? fboB : fboA;
        pass->apply(src, dst, controlState, liveParams);
        outputIsA = !outputIsA;
    }
}

ofFbo& ShaderChain::getOutputFbo() {
    return outputIsA ? fboA : fboB;
}
