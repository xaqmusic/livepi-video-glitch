#include "StutterBufferPass.h"

#include "ofGraphics.h"
#include "util/ShaderLoader.h"

void StutterBufferPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/stutter_hold.frag");
}

void StutterBufferPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const Scene& scene) {
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
    // we're iterating on real footage.
    bool stutterActive = scene.stutterEnabled && (controlState.midiClockTicks % 6 < 2);
    ofFbo& sourceForOutput = stutterActive ? ring[writeIndex % 3] : src;

    writeIndex = (writeIndex + 1) % kRingSize;

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    shader.setUniformTexture("srcTex", sourceForOutput.getTexture(), 0);
    sourceForOutput.draw(0, 0);
    shader.end();
    dst.end();
}
