#include "HSyncTearPass.h"

#include "ofAppRunner.h"
#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

void HSyncTearPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/hsync_tear.frag");
}

void HSyncTearPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) {
    if (controlState.beatInBar != lastBeatSeen) {
        lastBeatSeen = controlState.beatInBar;
        beatSpike = 1.0f;
    } else {
        beatSpike *= 0.9f;
    }
    noisePhase += ofGetLastFrameTime();

    // All modulation (knobs, audio bands, browser nudges) arrives through
    // the resolved param -- the old knobA/knobB master-intensity behavior is
    // now just a CC mapping fanning out to the three passes' intensities
    // (see the starter show's fixture mapping).
    float intensity = liveParams.getParam("hsync.intensity", 0.0f);

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("srcTex", src.getTexture(), 0);
    shader.setUniform1f("noisePhase", noisePhase);
    shader.setUniform1f("beatSpike", beatSpike);
    shader.setUniform1f("intensity", intensity);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    dst.end();
}
