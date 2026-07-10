#include "ChromaticAberrationPass.h"

#include "ofGraphics.h"
#include "ofUtils.h"
#include "util/ShaderLoader.h"

void ChromaticAberrationPass::setup() {
    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/chromatic_aberration.frag");
}

void ChromaticAberrationPass::apply(ofFbo& src, ofFbo& dst, const ControlState& controlState, const LiveParams& liveParams) {
    // The HLD's "tie separation to master audio or a MIDI CC" is now show
    // data, not pass code: an audioBand mapping adds the audio contribution
    // on top of whatever the knob/baseline sets, per scene, all resolved
    // into this one param before the pass runs.
    float separation = ofClamp(liveParams.getParam("chromatic.intensity", 0.0f), 0.0f, 1.0f);

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
