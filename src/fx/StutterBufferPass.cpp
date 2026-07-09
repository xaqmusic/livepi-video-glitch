#include "StutterBufferPass.h"

#include "ofGraphics.h"
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

    // Placeholder trigger: loop the last 3 buffered frames on every 16th
    // note (6 MIDI clock ticks), per the HLD's "loops 3 frames rapidly over
    // a 16th note." Exact beat-to-event mapping is scene content design, not
    // architecture -- see docs/architecture.md -- and will get tuned once
    // we're iterating on real footage. Gated on clockPresent: without a real
    // clock, midiClockTicks never advances past 0, and 0 % 6 < 2 would
    // otherwise make this permanently true instead of gracefully idle.
    // "stutter.enabled" resolves live, so a mapping can gate it from a knob.
    bool stutterActive = liveParams.getParam("stutter.enabled", 1.0f) > 0.5f && controlState.clockPresent
        && (controlState.midiClockTicks % 6 < 2);
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
