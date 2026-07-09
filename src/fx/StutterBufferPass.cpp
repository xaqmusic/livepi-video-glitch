#include "StutterBufferPass.h"

#include <algorithm>
#include <cmath>

#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

namespace {

// "stutter.rate" (0..1) quantizes to musical note lengths, in beats.
// HIGH = FAST: a velocity-mapped key stutters quicker the harder it's
// hit (performer request). Matches the manifest's enum label order:
// 1 bar, 1/2, 1/4, 1/8, 1/16.
float rateToBeats(float rate) {
    if (rate < 0.125f) return 4.0f;   // full bar
    if (rate < 0.375f) return 2.0f;   // 1/2 note
    if (rate < 0.625f) return 1.0f;   // 1/4 note
    if (rate < 0.875f) return 0.5f;   // 1/8 note
    return 0.25f;                     // 1/16 note
}

}  // namespace

void StutterBufferPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/stutter_hold.frag");
}

void StutterBufferPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState,
                              const LiveParams& liveParams) {
    double now = ofGetElapsedTimef();
    bool engageRequested = readParam(liveParams, "stutter.engage", 0.0f) > 0.5f;

    if (engageRequested && !engaged) {
        // Capture: every recorded frame inside the last interval, oldest
        // first. Interval from the rate param against the current tempo --
        // bpmEstimate free-runs at a musical default when no clock is
        // present, so this works with or without a synced keyboard.
        double bpm = controlState.bpmEstimate > 1.0 ? controlState.bpmEstimate : 120.0;
        double intervalSecs = rateToBeats(readParam(liveParams, "stutter.rate", 0.5f)) * 60.0 / bpm;

        std::vector<std::pair<double, int>> window;
        int newest = -1;
        double newestTime = -1.0;
        for (int i = 0; i < kRingCapacity; i++) {
            double t = ring[i].timeSeconds;
            if (t < 0) continue;
            if (t >= now - intervalSecs) window.push_back({t, i});
            if (t > newestTime) {
                newestTime = t;
                newest = i;
            }
        }
        std::sort(window.begin(), window.end());
        loopSlots.clear();
        for (const auto& [t, i] : window) loopSlots.push_back(i);
        // Interval shorter than one frame (or no history yet): freeze the
        // newest frame rather than doing nothing.
        if (loopSlots.empty() && newest >= 0) loopSlots.push_back(newest);
        playIndex = 0;
        engaged = !loopSlots.empty();
    } else if (!engageRequested && engaged) {
        engaged = false;
        loopSlots.clear();
    }

    const ofFbo* output = &src;

    if (engaged) {
        // Step through the captured window one frame per render, wrapping.
        // Recording pauses while engaged, so the window can't be
        // overwritten no matter how long the stutter is held.
        output = &ring[loopSlots[playIndex]].fbo;
        playIndex = (playIndex + 1) % loopSlots.size();
    } else {
        // Record continuously so an engage always has a fresh interval of
        // history behind it. Lazy per-slot allocation: one FBO per frame as
        // the ring first fills, no startup burst.
        Slot& slot = ring[writeIndex];
        if (!slot.fbo.isAllocated()) {
            slot.fbo.allocate(src.getWidth(), src.getHeight(), GL_RGBA);
        }
        slot.fbo.begin();
        src.draw(0, 0);
        slot.fbo.end();
        slot.timeSeconds = now;
        writeIndex = (writeIndex + 1) % kRingCapacity;
    }

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("srcTex", output->getTexture(), 0);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    dst.end();
}
