#include "LayerCompositor.h"

#include "ofGraphics.h"
#include "util/ShaderLoader.h"

void LayerCompositor::setup(int width, int height) {
    ofFboSettings settings;
    settings.width = width;
    settings.height = height;
    settings.internalformat = GL_RGBA;
    fboA.allocate(settings);
    fboB.allocate(settings);

    ShaderLoader::load(shader, "shaders/passthrough.vert", "shaders/composite_blend.frag");
    reset();
}

void LayerCompositor::reset() {
    resultIsA = true;
    fboA.begin();
    ofClear(0, 0, 0, 255);
    fboA.end();
}

void LayerCompositor::addLayer(const ofTexture& layerTex, BlendMode blendMode, float opacity) {
    ofFbo& accum = resultIsA ? fboA : fboB;
    ofFbo& dst = resultIsA ? fboB : fboA;

    dst.begin();
    ofClear(0, 0, 0, 255);
    shader.begin();
    ShaderLoader::bindMvp(shader);
    shader.setUniformTexture("accumTex", accum.getTexture(), 0);
    shader.setUniformTexture("layerTex", layerTex, 1);
    shader.setUniform1i("blendMode", static_cast<int>(blendMode));
    shader.setUniform1f("opacity", opacity);
    ShaderLoader::drawFullscreenQuad(dst.getWidth(), dst.getHeight());
    shader.end();
    dst.end();

    resultIsA = !resultIsA;
}

ofFbo& LayerCompositor::getResult() {
    return resultIsA ? fboA : fboB;
}
