#include "StutterBufferPass.h"

#include "ofGraphics.h"
#include "ofMath.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

void StutterBufferPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/stutter_hold.frag");
}

void StutterBufferPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) {
    // Allocate ring FBOs lazily once we know src's size -- ShaderChain always
    // runs passes at the chain's fixed resolution, so this only happens once.
    if (!ring[0].isAllocated()) {
        for (auto& fbo : ring) {
            fbo.allocate(src.getWidth(), src.getHeight(), GL_RGBA);
        }
    }

    // Always keep the ring current so a freeze/loop trigger has recent
    // frames to fall back on, whether or not we're stuttering this frame.
    ring[writeIndex].begin();
    src.draw(0, 0);
    ring[writeIndex].end();

    // Per-layer "stutter.amount" (0..1): 0 = off; rising amount makes the
    // hold windows both denser and longer, until ~1.0 is a near-constant
    // 3-frame loop. Deliberately TIME-based, not MIDI-clock-based -- the
    // original clock-gated trigger meant stutter silently never fired
    // without a running clock (exactly what happened in the first real
    // walkthrough). When a clock IS present, the window period snaps to a
    // 16th note so the chops land musically; free-run uses a fixed period.
    float amount = readParam(liveParams, "stutter.amount", 0.0f);
    bool stutterActive = false;
    if (amount > 0.01f) {
        float period;
        if (controlState.clockPresent && controlState.bpmEstimate > 1.0) {
            period = static_cast<float>(60.0 / controlState.bpmEstimate / 4.0);  // 16th note
        } else {
            period = ofLerp(0.7f, 0.16f, amount);
        }
        float holdFraction = ofLerp(0.2f, 0.97f, amount);
        stutterActive = fmodf(ofGetElapsedTimef(), period) < period * holdFraction;
    }
    ofFbo& sourceForOutput = stutterActive ? ring[writeIndex % 3] : src;

    writeIndex = (writeIndex + 1) % kRingSize;

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("srcTex", sourceForOutput.getTexture(), 0);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    dst.end();
}
