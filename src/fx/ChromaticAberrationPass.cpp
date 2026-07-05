#include "ChromaticAberrationPass.h"

#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

void ChromaticAberrationPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/chromatic_aberration.frag");
}

void ChromaticAberrationPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const Scene& scene) {
    // Per the HLD: "tie separation distance to the amplitude of your master
    // audio or a specific MIDI CC" -- blend both rather than picking one.
    float separation = ofClamp(controlState.audioLevel * 0.7f + scene.chromaticIntensity * 0.3f, 0.0f, 1.0f);

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    shader.setUniformTexture("srcTex", src.getTexture(), 0);
    shader.setUniform1f("separation", separation);
    src.draw(0, 0);
    shader.end();
    dst.end();
}
