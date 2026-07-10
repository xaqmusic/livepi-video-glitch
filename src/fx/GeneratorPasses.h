#pragma once

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
// velocity -> brightness), so a held key grows a beam and releasing it
// lets the streak fly off, dissolving in a fade zone before the exit
// edge. Trails dim per pixel traveled; a colorize shader maps pitch
// columns through the palette and the nearest-filtered upscale keeps it
// chunky raster, not smooth gradient.
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
};

// The classic demoscene fire: a GPU feedback sim (ping-pong heat field --
// each cell pulls from the cells toward the seed edge and cools a
// jittered amount, seed embers flicker at the origin edge) plus a
// palette colorize with straight alpha, so flames burn transparently
// over whatever plays beneath the layer. Sim rate is wall-clock fixed:
// flames climb at the same speed at 60fps desktop and 30fps Pi.
class FirePass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    const std::string& getName() const override { return name; }

private:
    ofShader stepShader;
    ofShader colorizeShader;
    std::string name = "fire";
    ofFbo heat[2];  // ping-pong sim buffers, low-res
    int frontIndex = 0;
    float stepAccum = 0.0f;
    float seedPhase = 0.0f;
};

// Layer `source` name -> pass, for SceneRenderer::loadScene. Unknown name
// returns nullptr and the layer renders black (same contract as an
// unresolved clipId: log, never crash).
std::unique_ptr<ShaderPass> makeGeneratorPass(const std::string& source);
