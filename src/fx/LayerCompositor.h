#pragma once

#include "ofFbo.h"
#include "ofShader.h"
#include "scenes/Scene.h"

// Accumulates a scene's layer stack bottom-to-top into one frame: reset()
// clears to black, addLayer() blends each layer's texture onto the running
// composite via composite_blend.frag (blend mode + opacity), getResult()
// is what feeds the scene-level post chain. Every layer -- including the
// first -- goes through the same blend shader over the black base, so
// opacity means the same thing at every stack position.
class LayerCompositor {
public:
    void setup(int width, int height);

    void reset();
    void addLayer(const ofTexture& layerTex, BlendMode blendMode, float opacity);
    ofFbo& getResult();

private:
    ofShader shader;
    ofFbo fboA, fboB;
    bool resultIsA = true;
};
