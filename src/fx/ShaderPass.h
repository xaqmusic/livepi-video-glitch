#pragma once

#include <string>

#include "control/ControlState.h"
#include "ofFbo.h"
#include "scenes/LiveParams.h"

// One stage in the glitch chain. Implementations own their own ofShader and
// any extra state (e.g. StutterBufferPass's frame ring buffer); ShaderChain
// only knows how to feed one pass's output into the next.
//
// controlState carries what changes every frame (beat, audio bands, clock);
// liveParams is the resolved parameter view -- the mapping resolver's live
// values (knobs, audio-band modulation, browser nudges) overlaid on the
// scene's static baselines. Passes read their own namespaced keys through
// liveParams.getParam(key, builtInDefault) and never look at raw CCs.
class ShaderPass {
public:
    virtual ~ShaderPass() = default;

    virtual void setup() = 0;
    virtual void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) = 0;
    virtual const std::string& getName() const = 0;

    // Chains SKIP inactive passes entirely -- no draw, no ping-pong flip --
    // so a dozen effects existing costs nothing until a scene dials one up
    // (docs/videosynth-effects.md, architecture implication #2). Default is
    // always-active; effect passes override to check their master param
    // against its neutral value. Passes with side effects every frame
    // (StutterBufferPass records history while idle) must stay always-on.
    virtual bool isActive(const LiveParams& liveParams) const {
        (void)liveParams;
        return true;
    }

    // A pass instantiated inside a LAYER's chain reads that layer's params
    // instead of scene-scope post-effect params -- SceneRenderer sets this
    // when it builds layer runtimes; scene-level post passes leave it empty.
    void setLayerId(const std::string& id) { layerId = id; }

protected:
    float readParam(const LiveParams& liveParams, const std::string& key, float fallback) const {
        return layerId.empty() ? liveParams.getParam(key, fallback)
                               : liveParams.getLayerParam(layerId, key, fallback);
    }

    std::string layerId;
};
