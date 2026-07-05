#pragma once

#include <string>

#include "control/ControlState.h"
#include "ofFbo.h"
#include "scenes/Scene.h"

// One stage in the glitch chain. Implementations own their own ofShader and
// any extra state (e.g. StutterBufferPass's frame ring buffer); ShaderChain
// only knows how to feed one pass's output into the next.
//
// controlState carries what changes every frame (beat, knobs, audio level);
// scene carries the current scene's preset intensities, which only change on
// a scene switch. Passes combine the two however makes sense for that effect.
class ShaderPass {
public:
    virtual ~ShaderPass() = default;

    virtual void setup() = 0;
    virtual void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const Scene& scene) = 0;
    virtual const std::string& getName() const = 0;
};
