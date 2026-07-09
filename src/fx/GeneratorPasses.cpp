#include "GeneratorPasses.h"

#include <cmath>

#include "ofAppRunner.h"
#include "ofGraphics.h"
#include "util/ShaderLoader.h"

namespace {

// Like FilterPasses' drawPass but with no srcTex bind -- generators paint.
template <typename BindUniforms>
void paintPass(ofShader& shader, ofFbo& dst, BindUniforms bindUniforms) {
    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    bindUniforms(shader);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    dst.end();
}

}  // namespace

// --- Plasma -------------------------------------------------------------

void PlasmaPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/plasma.frag");
}

void PlasmaPass::apply(ofFbo&, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float scaleRaw = readParam(liveParams, "plasma.scale", 0.4f);
    float speed = readParam(liveParams, "plasma.speed", 0.3f);
    float paletteRaw = readParam(liveParams, "plasma.palette", 0.0f);
    phase += speed * 3.0f * static_cast<float>(ofGetLastFrameTime());

    paintPass(shader, dst, [&](ofShader& sh) {
        sh.setUniform1f("phase", phase);
        // 0..1 -> 2..12: sparse slow blobs up to busy interference.
        sh.setUniform1f("plasmaScale", 2.0f + scaleRaw * 10.0f);
        sh.setUniform1i("paletteId", static_cast<int>(std::lround(paletteRaw * 2.0f)));
    });
}

// --- Copper bars ----------------------------------------------------------

void CopperBarsPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/copper_bars.frag");
}

void CopperBarsPass::apply(ofFbo&, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float barsRaw = readParam(liveParams, "copper.bars", 0.3f);
    float sharpness = readParam(liveParams, "copper.sharpness", 0.5f);
    float speed = readParam(liveParams, "copper.speed", 0.3f);
    float paletteRaw = readParam(liveParams, "copper.palette", 0.0f);
    phase += speed * 4.0f * static_cast<float>(ofGetLastFrameTime());

    paintPass(shader, dst, [&](ofShader& sh) {
        sh.setUniform1f("phase", phase);
        sh.setUniform1f("barCount", 1.0f + barsRaw * 9.0f);
        sh.setUniform1f("sharpness", sharpness);
        sh.setUniform1i("paletteId", static_cast<int>(std::lround(paletteRaw * 2.0f)));
    });
}

// --- Starfield ------------------------------------------------------------

void StarfieldPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/starfield.frag");
}

void StarfieldPass::apply(ofFbo&, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float density = readParam(liveParams, "star.density", 0.5f);
    float speed = readParam(liveParams, "star.speed", 0.3f);
    phase += speed * 2.0f * static_cast<float>(ofGetLastFrameTime());

    paintPass(shader, dst, [&](ofShader& sh) {
        sh.setUniform1f("phase", phase);
        sh.setUniform1f("density", density);
    });
}

// --- Factory ----------------------------------------------------------------

std::unique_ptr<ShaderPass> makeGeneratorPass(const std::string& source) {
    if (source == "plasma") return std::make_unique<PlasmaPass>();
    if (source == "copper") return std::make_unique<CopperBarsPass>();
    if (source == "starfield") return std::make_unique<StarfieldPass>();
    return nullptr;
}
