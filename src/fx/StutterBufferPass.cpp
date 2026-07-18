#include "StutterBufferPass.h"

#include <algorithm>
#include <cmath>

#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

namespace {

// "stutter.rate" (0..1) quantizes to musical note lengths, in beats.
// HIGH = FAST: a velocity-mapped key stutters quicker the harder it's
// hit. Matches the manifest's enum label order: 1 bar ... 1/32. At 1/32
// the interval is usually shorter than one rendered frame, so the
// capture falls through to its single-frame freeze -- an intentional
// hard-freeze at the top of the range.
float rateToBeats(float rate) {
    if (rate < 0.1f) return 4.0f;     // full bar
    if (rate < 0.3f) return 2.0f;     // 1/2 note
    if (rate < 0.5f) return 1.0f;     // 1/4 note
    if (rate < 0.7f) return 0.5f;     // 1/8 note
    if (rate < 0.9f) return 0.25f;    // 1/16 note
    return 0.125f;                    // 1/32 note
}

}  // namespace

void StutterBufferPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/stutter_hold.frag");
}

void StutterBufferPass::recordFrame(ofFbo& src, double now) {
    Slot& slot = ring[writeIndex];
    if (!slot.fbo.isAllocated()) {
        slot.fbo.allocate(src.getWidth(), src.getHeight(), GL_RGBA);
    }
    // Verbatim copy, blending OFF: transparent generator layers (note lasers,
    // fire) must keep their alpha through the ring -- an alpha blend here would
    // flatten a=1 and their black would go opaque, covering every layer beneath
    // (found via the laser overlay test).
    slot.fbo.begin();
    ofClear(0, 0, 0, 0);
    ofEnableBlendMode(OF_BLENDMODE_DISABLED);
    src.draw(0, 0);
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    slot.fbo.end();
    slot.timeSeconds = now;
    writeIndex++;
}

void StutterBufferPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState,
                              const LiveParams& liveParams) {
    double now = ofGetElapsedTimef();
    bool engageRequested = readParam(liveParams, "stutter.engage", 0.0f) > 0.5f;

    const ofFbo* output = &src;

    if (!engageRequested) {
        // IDLE: record nothing (the whole point -- no ring allocation, no
        // per-frame full-screen copy when stutter isn't in use). Reset so the
        // next engage starts a clean forward capture; pass the live feed on.
        capturing = false;
        engaged = false;
        loopSlots.clear();
    } else {
        // Interval from the rate param against the current tempo -- bpmEstimate
        // free-runs at a musical default when no clock is present, so this
        // works with or without a synced keyboard.
        double bpm = controlState.bpmEstimate > 1.0 ? controlState.bpmEstimate : 120.0;
        double intervalSecs = rateToBeats(readParam(liveParams, "stutter.rate", 0.5f)) * 60.0 / bpm;

        if (!capturing && !engaged) {
            // Rising edge: start capturing an interval FORWARD from now.
            capturing = true;
            captureStartSecs = now;
            writeIndex = 0;
        }

        if (capturing) {
            if (writeIndex < kRingCapacity) recordFrame(src, now);
            // Show the live frame while the window fills, so there's no visual
            // gap before the loop kicks in.
            bool full = (now - captureStartSecs) >= intervalSecs || writeIndex >= kRingCapacity;
            if (full) {
                loopSlots.clear();
                for (int i = 0; i < writeIndex; i++) loopSlots.push_back(i);
                // Sub-frame interval (rate faster than the frame time): the one
                // captured frame becomes a hard freeze.
                if (loopSlots.empty()) loopSlots.push_back(0);
                playIndex = 0;
                capturing = false;
                engaged = true;
            }
        } else if (engaged && !loopSlots.empty()) {
            // Step through the captured window one frame per render, wrapping.
            // Nothing records now, so the window can't be overwritten however
            // long the stutter is held.
            output = &ring[loopSlots[playIndex]].fbo;
            playIndex = (playIndex + 1) % loopSlots.size();
        }
    }

    dst.begin();
    ofClear(0, 0, 0, 0);
    ofEnableBlendMode(OF_BLENDMODE_DISABLED);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("srcTex", output->getTexture(), 0);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    dst.end();
}
