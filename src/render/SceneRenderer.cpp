#include "SceneRenderer.h"

#include <sstream>

#include "ofGraphics.h"
#include "ofLog.h"

void SceneRenderer::setup(int w, int h) {
    width = w;
    height = h;

    compositor.setup(width, height);
    postChain.setup(width, height);

    ofFboSettings settings;
    settings.width = width;
    settings.height = height;
    settings.internalformat = GL_RGBA;
    outputFbo.allocate(settings);
    outputFbo.begin();
    ofClear(0, 0, 0, 255);
    outputFbo.end();

    blackFbo.allocate(settings);
    blackFbo.begin();
    ofClear(0, 0, 0, 255);
    blackFbo.end();
}

void SceneRenderer::addPostPass(std::unique_ptr<ShaderPass> pass) {
    postChain.addPass(std::move(pass));
}

void SceneRenderer::loadScene(const Scene& scene) {
    // Destroy old runtimes (and their decoder sessions) BEFORE creating new
    // ones -- never overlap scenes' pipelines on the shared v4l2 block.
    runtimes.clear();

    for (const auto& layer : scene.layers) {
        auto runtime = std::make_unique<LayerRuntime>();
        runtime->layerId = layer.id;
        runtime->kind = layer.kind;
        runtime->chain.setup(width, height);  // empty pass list: seed-only until layerEffects passes exist

        if (layer.kind == LayerKind::Clip) {
            runtime->player = std::make_unique<ClipPlayer>();
            if (!layer.resolvedPath.empty()) {
                runtime->player->load(layer.resolvedPath);
            } else {
                ofLogWarning("SceneRenderer") << "Scene \"" << scene.name << "\" layer \"" << layer.id
                                              << "\" has no resolved clip -- rendering black.";
            }
        }
        runtimes.push_back(std::move(runtime));
    }
}

void SceneRenderer::update() {
    for (auto& runtime : runtimes) {
        if (runtime->player) runtime->player->update();
    }
}

bool SceneRenderer::layersReady() const {
    for (const auto& runtime : runtimes) {
        if (runtime->player && runtime->player->isLoaded() && !runtime->player->hasReceivedFrame()) {
            return false;
        }
    }
    return !runtimes.empty();
}

void SceneRenderer::render(const ControlState& controlState, const Scene& scene) {
    // Freeze-frame during scene switches: leave the previous output alone
    // until every new clip layer has a real decoded frame to show.
    if (!layersReady()) return;

    compositor.reset();
    for (const auto& runtime : runtimes) {
        // Blend mode / opacity read fresh from the Scene each frame (looked
        // up by stable layerId) so hot-reloaded param edits apply without
        // touching the runtime.
        const Layer* layer = nullptr;
        for (const auto& l : scene.layers) {
            if (l.id == runtime->layerId) {
                layer = &l;
                break;
            }
        }
        if (!layer) continue;  // runtime for a layer the scene no longer has

        if (runtime->player && runtime->player->isLoaded()) {
            runtime->chain.process(runtime->player->getDrawable(), controlState, scene);
        } else {
            // Generator placeholder / unresolved clip: black.
            runtime->chain.process(blackFbo, controlState, scene);
        }
        compositor.addLayer(runtime->chain.getOutputFbo().getTexture(), layer->blendMode, layer->opacity);
    }

    postChain.process(compositor.getResult(), controlState, scene);

    outputFbo.begin();
    ofClear(0, 0, 0, 255);
    ofSetColor(255);
    postChain.getOutputFbo().draw(0, 0, width, height);
    outputFbo.end();

}

std::string SceneRenderer::describeLayers() const {
    std::stringstream ss;
    for (size_t i = 0; i < runtimes.size(); i++) {
        const auto& runtime = runtimes[i];
        ss << "layer " << i << ": ";
        if (runtime->player) {
            ss << runtime->player->getTexture().getWidth() << "x" << runtime->player->getTexture().getHeight() << " "
               << runtime->player->getPixelFormatName() << "  pos: "
               << (runtime->player->getPosition() * runtime->player->getDuration()) << "s /"
               << runtime->player->getDuration() << "s";
        } else {
            ss << "generator (placeholder)";
        }
        if (i + 1 < runtimes.size()) ss << "\n";
    }
    return ss.str();
}
