#include "GeneratorPasses.h"

#include <algorithm>
#include <cmath>

#include "ofAppRunner.h"
#include "ofGraphics.h"
#include "ofMesh.h"
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

// --- Note lasers ------------------------------------------------------------

namespace {
// Buffer resolution: small enough to be free on the Pi, chunky enough to
// read as 90s raster once nearest-upscaled to the frame.
constexpr int kRollW = 320;
constexpr int kRollH = 180;
// Keyboard spread: C2..C7 (a 61-key) across the full width; out-of-range
// notes clamp to the edges.
constexpr int kNoteLo = 36;
constexpr int kNoteHi = 96;
}  // namespace

void LaserRollPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/laser_colorize.frag");
    for (auto& fbo : roll) {
        fbo.allocate(kRollW, kRollH, GL_RGBA);
        fbo.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        fbo.begin();
        ofClear(0, 0, 0, 255);
        fbo.end();
    }
}

void LaserRollPass::apply(ofFbo&, ofFbo& dst, const ControlState& controlState,
                          const LiveParams& liveParams) {
    bool down = readParam(liveParams, "laser.direction", 0.0f) >= 0.5f;
    float speed = readParam(liveParams, "laser.speed", 0.5f);
    float fade = readParam(liveParams, "laser.fade", 0.7f);
    float widthRaw = readParam(liveParams, "laser.width", 0.3f);
    float paletteRaw = readParam(liveParams, "laser.palette", 0.0f);

    // Integer-pixel scrolling with a fractional accumulator: frame-rate
    // independent without ever sampling between texels (which would blur
    // the raster edges).
    scrollAccum += (30.0f + speed * 170.0f) * static_cast<float>(ofGetLastFrameTime());
    int px = static_cast<int>(scrollAccum);
    scrollAccum -= px;

    if (px > 0) {
        ofFbo& front = roll[frontIndex];
        ofFbo& back = roll[frontIndex ^ 1];
        back.begin();
        ofClear(0, 0, 0, 255);
        // Trail persistence per PIXEL TRAVELED (keep^px), so trail length
        // is independent of frame rate and scroll speed: fade 0 dies in
        // ~80 buffer px, fade 1 never dims. 8-bit quantization of the
        // repeated multiply gives banded falloff -- authentically retro.
        float keepPerPx = 0.97f + fade * 0.03f;
        ofSetColor(static_cast<int>(255.0f * std::pow(keepPerPx, static_cast<float>(px))));
        front.draw(0, down ? px : -px);

        float beamW = 1.0f + std::floor(widthRaw * 5.0f);
        int headH = px + 2;
        for (const auto& [note, vel] : controlState.noteValues) {
            if (vel <= 0.01f) continue;
            float x01 = std::min(std::max((note - kNoteLo) / static_cast<float>(kNoteHi - kNoteLo), 0.0f), 1.0f);
            float x = x01 * (kRollW - beamW);
            ofSetColor(static_cast<int>(150.0f + vel * 105.0f));
            ofDrawRectangle(x, down ? 0.0f : static_cast<float>(kRollH - headH), beamW,
                            static_cast<float>(headH));
        }

        // Tail dissolve: a multiply-gradient over the last stretch before
        // the exit edge. It compounds as content travels through, so a
        // streak melts to black before leaving instead of sliding off as
        // a hard-ended bar. Done in buffer space (not the colorize
        // shader) so the edge is unambiguous regardless of GL/GLES FBO
        // texture orientation.
        constexpr float kFadeZone = 0.22f;
        float zoneH = kRollH * kFadeZone;
        float yEdge = down ? static_cast<float>(kRollH) : 0.0f;
        float yInner = down ? kRollH - zoneH : zoneH;
        ofMesh fadeMesh;
        fadeMesh.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
        fadeMesh.addVertex(glm::vec3(0, yEdge, 0));
        fadeMesh.addColor(ofFloatColor(0.6f, 0.6f, 0.6f));
        fadeMesh.addVertex(glm::vec3(kRollW, yEdge, 0));
        fadeMesh.addColor(ofFloatColor(0.6f, 0.6f, 0.6f));
        fadeMesh.addVertex(glm::vec3(0, yInner, 0));
        fadeMesh.addColor(ofFloatColor(1.0f, 1.0f, 1.0f));
        fadeMesh.addVertex(glm::vec3(kRollW, yInner, 0));
        fadeMesh.addColor(ofFloatColor(1.0f, 1.0f, 1.0f));
        ofSetColor(255);
        ofEnableBlendMode(OF_BLENDMODE_MULTIPLY);
        fadeMesh.draw();
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);

        back.end();
        frontIndex ^= 1;
    }

    // Transparent background: only the beams carry alpha (the colorize
    // shader sets a = intensity). Blending must be OFF so the straight-
    // alpha fragments land in the FBO verbatim instead of premultiplying
    // against the cleared black.
    dst.begin();
    ofClear(0, 0, 0, 0);
    ofEnableBlendMode(OF_BLENDMODE_DISABLED);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("srcTex", roll[frontIndex].getTexture(), 0);
    shader.setUniform1i("paletteId", static_cast<int>(std::lround(paletteRaw * 2.0f)));
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    dst.end();
}

// --- Fire ---------------------------------------------------------------

namespace {
constexpr int kFireW = 320;
constexpr int kFireH = 180;
// Sim steps per wall-clock second: flames climb identically at desktop
// 60fps and Pi 30fps.
constexpr float kFireStepsPerSec = 45.0f;
}  // namespace

void FirePass::setup() {
    ShaderLoader::load(stepShader, "shaders/passthrough.vert", "shaders/fire_step.frag");
    ShaderLoader::load(colorizeShader, "shaders/passthrough.vert", "shaders/fire_colorize.frag");
    for (auto& fbo : heat) {
        fbo.allocate(kFireW, kFireH, GL_RGBA);
        fbo.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        fbo.begin();
        ofClear(0, 0, 0, 255);
        fbo.end();
    }
}

void FirePass::apply(ofFbo&, ofFbo& dst, const ControlState&, const LiveParams& liveParams) {
    float height = readParam(liveParams, "fire.height", 0.6f);
    bool down = readParam(liveParams, "fire.direction", 0.0f) >= 0.5f;
    float paletteRaw = readParam(liveParams, "fire.palette", 0.0f);

    stepAccum += kFireStepsPerSec * static_cast<float>(ofGetLastFrameTime());
    int steps = std::min(static_cast<int>(stepAccum), 3);
    stepAccum -= static_cast<int>(stepAccum);

    for (int i = 0; i < steps; i++) {
        ofFbo& front = heat[frontIndex];
        ofFbo& back = heat[frontIndex ^ 1];
        seedPhase += 1.0f;
        back.begin();
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        stepShader.begin();
        ShaderLoader::bindMvp(stepShader);
        stepShader.setUniformTexture("srcTex", front.getTexture(), 0);
        stepShader.setUniform2f("texel", 1.0f / kFireW, 1.0f / kFireH);
        stepShader.setUniform1f("seedPhase", seedPhase);
        stepShader.setUniform1f("height", height);
        stepShader.setUniform1f("down", down ? 1.0f : 0.0f);
        ShaderLoader::drawFullscreenQuad(kFireW, kFireH);
        stepShader.end();
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        back.end();
        frontIndex ^= 1;
    }

    // Transparent output: alpha comes from heat, cold cells leave the
    // layers below untouched. Blending off so straight alpha lands
    // verbatim (same rule as the laser colorize).
    dst.begin();
    ofClear(0, 0, 0, 0);
    ofEnableBlendMode(OF_BLENDMODE_DISABLED);
    colorizeShader.begin();
    ShaderLoader::bindMvp(colorizeShader);
    colorizeShader.setUniformTexture("srcTex", heat[frontIndex].getTexture(), 0);
    colorizeShader.setUniform1i("paletteId", static_cast<int>(std::lround(paletteRaw * 2.0f)));
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    colorizeShader.end();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    dst.end();
}

// --- Note blobs -------------------------------------------------------------

namespace {
// Low-res field: chunky enough to read as pixelated raster once
// nearest-upscaled, cheap on the Pi.
constexpr int kBlobW = 160;
constexpr int kBlobH = 90;
// Decay applied at a fixed wall-clock rate: blobs fade the same at desktop
// 60fps and Pi 30fps.
constexpr float kBlobStepsPerSec = 60.0f;
// Spiral note range: C1..C7. Middle C (60) sits at t=0.5 -> the mid radius,
// so its octave forms the middle arc (the design ask). Out-of-range notes
// clamp to centre / rim.
constexpr int kBlobNoteLo = 24;
constexpr int kBlobNoteHi = 96;
constexpr float kGoldenAngle = 2.399963f;  // radians (~137.5deg): phyllotaxis spread
constexpr float kBlobMaxRadius = 30.0f;    // outermost note, buffer px (fits the 90px height)

// A soft radial blob: a triangle fan, bright centre -> transparent rim.
// Drawn under ADD blending so its alpha ramp deposits rgb*alpha radially and
// overlapping blobs sum like mixing light.
void drawGradientDisc(float cx, float cy, float radius, const ofFloatColor& rgb, float centerAlpha) {
    constexpr int kSegs = 28;
    ofMesh fan;
    fan.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
    fan.addVertex(glm::vec3(cx, cy, 0));
    fan.addColor(ofFloatColor(rgb.r, rgb.g, rgb.b, centerAlpha));
    for (int i = 0; i <= kSegs; i++) {
        float a = (i / static_cast<float>(kSegs)) * TWO_PI;
        fan.addVertex(glm::vec3(cx + std::cos(a) * radius, cy + std::sin(a) * radius, 0));
        fan.addColor(ofFloatColor(rgb.r, rgb.g, rgb.b, 0.0f));
    }
    fan.draw();
}
}  // namespace

void BlobsPass::setup() {
    ShaderLoader::load(colorizeShader, "shaders/passthrough.vert", "shaders/blobs_colorize.frag");
    for (auto& fbo : blob) {
        fbo.allocate(kBlobW, kBlobH, GL_RGBA);
        fbo.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        fbo.begin();
        ofClear(0, 0, 0, 255);
        fbo.end();
    }
}

void BlobsPass::apply(ofFbo&, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) {
    float sizeRaw = readParam(liveParams, "blobs.size", 0.4f);
    float spacing = readParam(liveParams, "blobs.spacing", 0.5f);
    float decay = readParam(liveParams, "blobs.decay", 0.5f);
    float pixelate = readParam(liveParams, "blobs.pixelate", 0.5f);
    float hueBase = readParam(liveParams, "blobs.hue", 0.0f);
    float spread = readParam(liveParams, "blobs.spread", 0.6f);

    // Spiral extent: scales the whole pattern tighter or wider around center.
    // 0.5 (default) == 1.0, reproducing the base spiral; 0 packs the notes
    // into a tight cluster, 1 flings the outer notes to the frame edge.
    float radiusScale = 0.35f + spacing * 1.3f;

    // Decay the whole field a fixed number of steps per wall-clock second.
    // keep-per-step maps decay 0 -> ~0.2s linger, 1 -> ~2s.
    float keep = 0.85f + decay * 0.135f;
    stepAccum += kBlobStepsPerSec * static_cast<float>(ofGetLastFrameTime());
    int steps = std::min(static_cast<int>(stepAccum), 3);
    stepAccum -= static_cast<int>(stepAccum);
    for (int i = 0; i < steps; i++) {
        ofFbo& front = blob[frontIndex];
        ofFbo& back = blob[frontIndex ^ 1];
        back.begin();
        // Blend off + a grey tint = an exact multiply-replace (field *= keep),
        // no compositing against the clear.
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        int k = static_cast<int>(keep * 255.0f);
        ofSetColor(k, k, k, 255);
        front.draw(0, 0, kBlobW, kBlobH);
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        back.end();
        frontIndex ^= 1;
    }

    // Onset stamps: a note that turned on THIS frame (0 -> >0) lights its
    // fixed spiral blob once, then decays. Held keys don't re-stamp -- each
    // press is one bloom -- and the stamp lands after the decay so it starts
    // this frame at full brightness.
    ofFbo& cur = blob[frontIndex];
    cur.begin();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    const float cx = kBlobW * 0.5f;
    const float cy = kBlobH * 0.5f;
    float baseR = 5.0f + sizeRaw * 9.0f;  // buffer px
    float curVel[128] = {};
    for (const auto& [note, vel] : controlState.noteValues) {
        if (note >= 0 && note < 128) curVel[note] = vel;
    }
    for (int n = 0; n < 128; n++) {
        if (curVel[n] > 0.01f && prevVel[n] <= 0.01f) {
            int nn = std::clamp(n, kBlobNoteLo, kBlobNoteHi);
            float t = (nn - kBlobNoteLo) / static_cast<float>(kBlobNoteHi - kBlobNoteLo);
            float angle = (nn - kBlobNoteLo) * kGoldenAngle;
            float px = cx + t * kBlobMaxRadius * radiusScale * std::cos(angle);
            float py = cy + t * kBlobMaxRadius * radiusScale * std::sin(angle);
            // Colour by pitch class (octaves share a hue); hue base + spread
            // are live-mappable, so 0 spread is monochrome and 1 a full wheel.
            float hue = hueBase + spread * ((n % 12) / 12.0f);
            hue -= std::floor(hue);
            ofFloatColor col = ofFloatColor::fromHsb(hue, 0.85f, 1.0f);
            float bright = 0.55f + 0.45f * curVel[n];
            drawGradientDisc(px, py, baseR * (0.7f + 0.3f * curVel[n]), col, bright);
        }
        prevVel[n] = curVel[n];
    }
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    cur.end();

    // Colorize: posterize the intensity for the chunky, stepped fade and key
    // transparency off it -- the layer is clear except for the blobs (straight
    // alpha, blending off, same rule as the laser/fire colorize).
    dst.begin();
    ofClear(0, 0, 0, 0);
    ofEnableBlendMode(OF_BLENDMODE_DISABLED);
    colorizeShader.begin();
    ShaderLoader::bindMvp(colorizeShader);
    colorizeShader.setUniformTexture("srcTex", blob[frontIndex].getTexture(), 0);
    colorizeShader.setUniform1f("pixelate", pixelate);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    colorizeShader.end();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    dst.end();
}

// --- Factory ----------------------------------------------------------------

std::unique_ptr<ShaderPass> makeGeneratorPass(const std::string& source) {
    if (source == "plasma") return std::make_unique<PlasmaPass>();
    if (source == "copper") return std::make_unique<CopperBarsPass>();
    if (source == "starfield") return std::make_unique<StarfieldPass>();
    if (source == "laser") return std::make_unique<LaserRollPass>();
    if (source == "fire") return std::make_unique<FirePass>();
    if (source == "blobs") return std::make_unique<BlobsPass>();
    return nullptr;
}
