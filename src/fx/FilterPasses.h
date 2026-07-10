#pragma once

#include <string>

#include "ShaderPass.h"
#include "ofFbo.h"
#include "ofShader.h"

// The videosynth filter round (docs/videosynth-effects.md): coordinate
// warps + the palette pass + the lens sim. Each is a thin ShaderPass --
// setup() loads its shader, apply() binds uniforms, isActive() checks its
// master param against neutral so idle effects cost nothing (the chain
// skips them entirely). All read params through readParam(), so the same
// class works layer-scoped (warps/palette, per docs/videosynth-backend.md)
// or scene-scoped (barrel, in the post chain).

// Spin + zoom around center, mirror-tiled edges. The classic Second
// Reality background at full tilt, a woozy wobble at low settings.
class RotozoomPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "rotozoom";
    float angle = 0.0f;
};

// Fold the frame into 3..12 mirrored wedges.
class KaleidoscopePass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "kaleidoscope";
};

// Vertical-strip sibling of HSyncTearPass: columns shear instead of
// scanlines, with the same beat spike.
class TwisterBarsPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "twister_bars";
    float noisePhase = 0.0f;
    float beatSpike = 0.0f;
    int lastBeatSeen = -1;
};

// Polar remap using the footage itself as the tunnel wall.
class TunnelPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "tunnel";
    float phase = 0.0f;
};

// Luma-quantized bins through a drifting cosine palette (lava/vaporwave/
// phosphor presets from palette.glslinc).
class PosterizeCyclePass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "posterize_cycle";
    float cyclePhase = 0.0f;
};

// Voronoi shatter: irregular rigid shards displace/tilt apart with
// `amount` and reassemble perfectly at 0; cracks between them go
// transparent. Random fracture, not a mosaic -- built for the
// post-apocalyptic "breaking apart, then coming back together" ask.
class FracturePass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "fracture";
    float phase = 0.0f;
    float jitterPhase = 0.0f;
};

// Brightness (gain) / contrast / saturation, all neutral at 0.5. Used in
// BOTH scopes: per layer (after the warps, before posterize quantizes)
// and scene-wide as the FIRST post pass (grade the composite before the
// CRT decay chews on it).
class ColorAdjustPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "color_adjust";
};

// Transition dip-to-black (scene-scope "transition.fade", driven by the
// renderer's transition ramp, not user mappings). Very last post pass.
class FadePass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "fade";
};

// The curved tube itself -- belongs LAST in the post chain.
class BarrelPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "barrel";
};

// CRT scan lines: horizontal dark lines darken the picture; `zoom` sets the
// line pitch so the tube reads as closer (few thick lines) or farther (fine
// and dense). Sits just before Barrel so the tube curves the lines with it.
class ScanlinesPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "scanlines";
};

// TV snow: animated grain mixed over the signal. `amount` fades from a light
// haze to a dead-channel snow field; `scale` sizes the grain (fine .. chunky
// blocks), `brightness` its white level, `blur` softens it from crisp specks
// to a cloudy wash. The FIRST post pass, so the rest of the CRT chain grades,
// tears and curves the noisy signal.
class StaticPass : public ShaderPass {
public:
    void setup() override;
    void apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) override;
    bool isActive(const LiveParams& liveParams) const override;
    const std::string& getName() const override { return name; }

private:
    ofShader shader;
    std::string name = "static";
    float phase = 0.0f;
};
