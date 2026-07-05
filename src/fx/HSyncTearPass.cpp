#include "HSyncTearPass.h"

#include "ofAppRunner.h"
#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

void HSyncTearPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/hsync_tear.frag");
}

void HSyncTearPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const Scene& scene) {
    if (controlState.beatInBar != lastBeatSeen) {
        lastBeatSeen = controlState.beatInBar;
        beatSpike = 1.0f;
    } else {
        beatSpike *= 0.9f;
    }
    noisePhase += ofGetLastFrameTime();

    float intensity = scene.hSyncIntensity * (0.5f + controlState.knobB * 0.5f);

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    shader.setUniformTexture("srcTex", src.getTexture(), 0);
    shader.setUniform1f("noisePhase", noisePhase);
    shader.setUniform1f("beatSpike", beatSpike);
    shader.setUniform1f("intensity", intensity);
    src.draw(0, 0);
    shader.end();
    dst.end();
}
