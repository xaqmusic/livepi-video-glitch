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
};
