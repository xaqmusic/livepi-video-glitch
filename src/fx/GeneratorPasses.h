#pragma once

#include <map>
#include <memory>
#include <string>

#include "ShaderPass.h"
#include "ofFbo.h"
#include "ofShader.h"

// Generator sources (docs/videosynth-effects.md): passes that PAINT rather
// than filter -- they ignore srcTex entirely and fill the layer FBO with
// procedural content. A generator sits FIRST in its layer's chain, so the
// whole effect stack (stutter, warps, posterize) applies to generated
// content exactly as it does to footage. Params live in the layer's
// `params` map, addressed "<generator>.<param>", live-mappable like any
// other layer param. Speeds integrate CPU-side (phase += speed * dt) so
// riding a speed knob never jumps the animation.

// Sum-of-sines through the shared cosine palette. THE demo effect.
class PlasmaPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "plasma";
    float phase = 0.0f;
};

// Horizontal palette bands sweeping the screen -- the Amiga Copper trick.
// Layer blend mode + opacity decide whether it tints or screens over the
// layers below.
class CopperBarsPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "copper_bars";
    float phase = 0.0f;
};

// Three parallax planes of hashed-grid stars.
class StarfieldPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "starfield";
    float phase = 0.0f;
};

// NOTE-TRIGGERED generator: reads controlState.noteValues directly (no
// mapping/learn step -- it hears whatever the keyboard plays). The
// piano-game waterfall as the classic 90s scroll-buffer trick: a tiny
// ping-pong buffer shifts a few pixels per frame in the chosen direction;
// held notes paint bright segments at the origin edge (pitch -> column,
// velocity -> brightness, onset -> wider hit flash), so a held key grows
// a beam and releasing it lets the streak fly off. Trails dim per scroll
// step; a colorize shader maps pitch columns through the palette and the
// nearest-filtered upscale keeps it chunky raster, not smooth gradient.
class LaserRollPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "laser_roll";
    ofFbo roll[2];  // ping-pong scroll buffers, low-res
    int frontIndex = 0;
    float scrollAccum = 0.0f;
    std::map<int, float> prevNotes;  // onset detection for the hit flash
};

// Layer `source` name -> pass, for SceneRenderer::loadScene. Unknown name
// returns nullptr and the layer renders black (same contract as an
// unresolved clipId: log, never crash).
std::unique_ptr<ShaderPass> makeGeneratorPass(const std::string& source);
