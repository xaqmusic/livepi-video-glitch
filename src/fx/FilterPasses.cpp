#include "FilterPasses.h"

#include <cmath>

#include "ofAppRunner.h"
#include "ofGraphics.h"
#include "util/ShaderLoader.h"

namespace {

constexpr float kNeutral = 0.005f;

// Shared draw boilerplate: every filter renders src -> dst through its
// shader with whatever uniforms the caller bound in `bindUniforms`.
template <typename BindUniforms>
void drawPass(ofShader& shader, ofFbo& src, ofFbo& dst, BindUniforms bindUniforms) {
    dst.begin();
    ofClear(0, 0, 0, 0);
    // Full overwrite, never a blend: passes must carry src ALPHA through
    // untouched (transparent generator layers keep their holes) rather
    // than compositing translucent fragments against the cleared black.
    ofEnableBlendMode(OF_BLENDMODE_DISABLED);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("srcTex", src.getTexture(), 0);
    bindUniforms(shader);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    dst.end();
}

}  // namespace

// --- Rotozoom ---------------------------------------------------------

void RotozoomPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/rotozoom.frag");
}

bool RotozoomPass::isActive(const LiveParams& liveParams) const {
    return std::fabs(readParam(liveParams, "rotozoom.speed", 0.0f)) > kNeutral
        || std::fabs(readParam(liveParams, "rotozoom.zoom", 0.0f)) > kNeutral;
}

void RotozoomPass::apply(ofFbo& src, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float speed = readParam(liveParams, "rotozoom.speed", 0.0f);
    float zoom = readParam(liveParams, "rotozoom.zoom", 0.0f);
    angle += speed * 2.0f * static_cast<float>(ofGetLastFrameTime());

    drawPass(shader, src, dst, [&](ofShader& sh) {
        sh.setUniform1f("angle", angle);
        sh.setUniform1f("zoomScale", std::exp2(-zoom * 1.5f));
    });
}

// --- Kaleidoscope ------------------------------------------------------

void KaleidoscopePass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/kaleidoscope.frag");
}

bool KaleidoscopePass::isActive(const LiveParams& liveParams) const {
    return readParam(liveParams, "kaleido.segments", 0.0f) > 0.02f;
}

void KaleidoscopePass::apply(ofFbo& src, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float raw = readParam(liveParams, "kaleido.segments", 0.0f);
    // 0..1 -> 3..12 wedges, integer-quantized: one knob, a completely
    // different symmetry per step.
    float segments = 3.0f + std::floor(raw * 9.99f);

    drawPass(shader, src, dst, [&](ofShader& sh) { sh.setUniform1f("segments", segments); });
}

// --- Twister bars ------------------------------------------------------

void TwisterBarsPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/twister_bars.frag");
}

bool TwisterBarsPass::isActive(const LiveParams& liveParams) const {
    return readParam(liveParams, "twister.intensity", 0.0f) > kNeutral;
}

void TwisterBarsPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState,
                            const LiveParams& liveParams) {
    if (controlState.beatInBar != lastBeatSeen) {
        lastBeatSeen = controlState.beatInBar;
        beatSpike = 1.0f;
    } else {
        beatSpike *= 0.9f;
    }
    noisePhase += static_cast<float>(ofGetLastFrameTime());
    float intensity = readParam(liveParams, "twister.intensity", 0.0f);

    drawPass(shader, src, dst, [&](ofShader& sh) {
        sh.setUniform1f("noisePhase", noisePhase);
        sh.setUniform1f("beatSpike", beatSpike);
        sh.setUniform1f("intensity", intensity);
    });
}

// --- Tunnel ------------------------------------------------------------

void TunnelPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/tunnel.frag");
}

bool TunnelPass::isActive(const LiveParams& liveParams) const {
    return readParam(liveParams, "tunnel.amount", 0.0f) > kNeutral;
}

void TunnelPass::apply(ofFbo& src, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float amount = readParam(liveParams, "tunnel.amount", 0.0f);
    float speed = readParam(liveParams, "tunnel.speed", 0.3f);
    phase += speed * 0.5f * static_cast<float>(ofGetLastFrameTime());

    drawPass(shader, src, dst, [&](ofShader& sh) {
        sh.setUniform1f("tunnelPhase", phase);
        sh.setUniform1f("amount", amount);
    });
}

// --- Posterize + color cycle -------------------------------------------

void PosterizeCyclePass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/posterize_cycle.frag");
}

bool PosterizeCyclePass::isActive(const LiveParams& liveParams) const {
    return readParam(liveParams, "posterize.amount", 0.0f) > kNeutral;
}

void PosterizeCyclePass::apply(ofFbo& src, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float amount = readParam(liveParams, "posterize.amount", 0.0f);
    float levelsRaw = readParam(liveParams, "posterize.levels", 0.5f);
    float speed = readParam(liveParams, "posterize.speed", 0.2f);
    float paletteRaw = readParam(liveParams, "posterize.palette", 0.0f);
    cyclePhase += speed * 0.3f * static_cast<float>(ofGetLastFrameTime());

    drawPass(shader, src, dst, [&](ofShader& sh) {
        // 0..1 -> 2..8 bins; coarser reads more retro.
        sh.setUniform1f("levels", 2.0f + std::floor(levelsRaw * 6.99f));
        sh.setUniform1f("cyclePhase", cyclePhase);
        sh.setUniform1i("paletteId", static_cast<int>(std::lround(paletteRaw * 2.0f)));
        sh.setUniform1f("amount", amount);
    });
}

// --- Fracture -------------------------------------------------------------

void FracturePass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/fracture.frag");
}

bool FracturePass::isActive(const LiveParams& liveParams) const {
    return readParam(liveParams, "fracture.amount", 0.0f) > kNeutral;
}

void FracturePass::apply(ofFbo& src, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float amount = readParam(liveParams, "fracture.amount", 0.0f);
    float piecesRaw = readParam(liveParams, "fracture.pieces", 0.4f);
    float drift = readParam(liveParams, "fracture.drift", 0.25f);
    float jitter = readParam(liveParams, "fracture.jitter", 0.0f);
    float scatter = readParam(liveParams, "fracture.scatter", 0.0f);
    float dt = static_cast<float>(ofGetLastFrameTime());
    phase += drift * 0.8f * dt;
    // Fixed twitch clock; jitterAmt scales how FAR pieces tremble, the
    // rate stays musical (~14 steps/s).
    jitterPhase += 14.0f * dt;

    drawPass(shader, src, dst, [&](ofShader& sh) {
        sh.setUniform1f("amount", amount);
        // 0..1 -> 3..20 shards across: a few big slabs up to gravel.
        sh.setUniform1f("cells", 3.0f + piecesRaw * 17.0f);
        sh.setUniform1f("phase", phase);
        sh.setUniform1f("jitterAmt", jitter);
        sh.setUniform1f("jitterStep", std::floor(jitterPhase));
        // 16 discrete arrangements across the scatter range: any bound
        // control crossing a step boundary re-rolls the debris.
        sh.setUniform1f("scatterStep", std::floor(scatter * 15.999f));
    });
}

// --- Color adjust --------------------------------------------------------

void ColorAdjustPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/color_adjust.frag");
}

bool ColorAdjustPass::isActive(const LiveParams& liveParams) const {
    return std::fabs(readParam(liveParams, "color.brightness", 0.5f) - 0.5f) > kNeutral
        || std::fabs(readParam(liveParams, "color.contrast", 0.5f) - 0.5f) > kNeutral
        || std::fabs(readParam(liveParams, "color.saturation", 0.5f) - 0.5f) > kNeutral;
}

void ColorAdjustPass::apply(ofFbo& src, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    // All three neutral at 0.5, doubling at 1, gone at 0.
    float gain = readParam(liveParams, "color.brightness", 0.5f) * 2.0f;
    float contrast = readParam(liveParams, "color.contrast", 0.5f) * 2.0f;
    float saturation = readParam(liveParams, "color.saturation", 0.5f) * 2.0f;

    drawPass(shader, src, dst, [&](ofShader& sh) {
        sh.setUniform1f("gain", gain);
        sh.setUniform1f("contrast", contrast);
        sh.setUniform1f("saturation", saturation);
    });
}

// --- Barrel ------------------------------------------------------------

void BarrelPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/barrel.frag");
}

bool BarrelPass::isActive(const LiveParams& liveParams) const {
    return readParam(liveParams, "barrel.amount", 0.0f) > kNeutral;
}

void BarrelPass::apply(ofFbo& src, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float amount = readParam(liveParams, "barrel.amount", 0.0f);
    drawPass(shader, src, dst, [&](ofShader& sh) { sh.setUniform1f("amount", amount); });
}
