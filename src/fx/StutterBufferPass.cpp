#include "StutterBufferPass.h"

#include <cmath>

#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

namespace {

// "stutter.rate" (0..1) quantizes to musical note lengths, in beats.
// Matches the manifest's enum labels: 1/16, 1/8, 1/4, 1/2, 1 bar (4/4).
float rateToBeats(float rate) {
    if (rate < 0.125f) return 0.25f;  // 1/16 note
    if (rate < 0.375f) return 0.5f;   // 1/8 note
    if (rate < 0.625f) return 1.0f;   // 1/4 note
    if (rate < 0.875f) return 2.0f;   // 1/2 note
    return 4.0f;                      // full bar
}

}  // namespace

void StutterBufferPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/stutter_hold.frag");
}

const ofFbo* StutterBufferPass::findFrameNear(double targetTime) const {
    const Slot* best = nullptr;
    double bestDelta = 1e9;
    for (const auto& slot : ring) {
        if (slot.timeSeconds < 0) continue;
        double delta = std::fabs(slot.timeSeconds - targetTime);
        if (delta < bestDelta) {
            bestDelta = delta;
            best = &slot;
        }
    }
    return best ? &best->fbo : nullptr;
}

void StutterBufferPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState,
                              const LiveParams& liveParams) {
    double now = ofGetElapsedTimef();
    bool engageRequested = readParam(liveParams, "stutter.engage", 0.0f) > 0.5f;

    if (engageRequested && !engaged) {
        engaged = true;
        engageTime = now;

        // Interval from the rate param against the current tempo --
        // bpmEstimate free-runs at a musical default when no clock is
        // present, so this works with or without a synced keyboard.
        double bpm = controlState.bpmEstimate > 1.0 ? controlState.bpmEstimate : 120.0;
        intervalSecs = rateToBeats(readParam(liveParams, "stutter.rate", 0.5f)) * 60.0 / bpm;

        // Can't loop further back than recorded history reaches.
        double oldest = now;
        for (const auto& slot : ring) {
            if (slot.timeSeconds >= 0 && slot.timeSeconds < oldest) oldest = slot.timeSeconds;
        }
        double available = now - oldest;
        if (available > 0.05 && intervalSecs > available) intervalSecs = available;
    } else if (!engageRequested && engaged) {
        engaged = false;
    }

    const ofFbo* output = &src;

    if (engaged) {
        // Loop the captured window [engageTime - interval, engageTime] at
        // normal speed: same phase math every frame, frames found by
        // timestamp. Recording is paused, so the window can't be
        // overwritten no matter how long the stutter is held.
        double phase = std::fmod(now - engageTime, intervalSecs);
        const ofFbo* looped = findFrameNear(engageTime - intervalSecs + phase);
        if (looped) output = looped;
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
