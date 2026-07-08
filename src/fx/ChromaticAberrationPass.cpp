#include "ChromaticAberrationPass.h"

#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

void ChromaticAberrationPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/chromatic_aberration.frag");
}

void ChromaticAberrationPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const Scene& scene) {
    // Per the HLD: "tie separation distance to the amplitude of your master
    // audio or a specific MIDI CC" -- blend audio, a live knob, and the
    // scene's baseline rather than picking just one.
    float separation = ofClamp(
        controlState.audioLevel * 0.4f + controlState.knobB * 0.3f + scene.chromaticIntensity * 0.3f, 0.0f, 1.0f);

    // knobA is bidirectional (-1..1, center-detent) -- remap to 0..1 so it
    // reads as a master glitch-intensity knob shared across all three passes,
    // fully counterclockwise kills the effect, fully clockwise is the
    // scene's configured intensity.
    float masterIntensity = ofClamp((controlState.knobA + 1.0f) * 0.5f, 0.0f, 1.0f);
    separation *= masterIntensity;

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("srcTex", src.getTexture(), 0);
    shader.setUniform1f("separation", separation);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    dst.end();
}
