#include "SceneRenderer.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "fx/FilterPasses.h"
#include "fx/GeneratorPasses.h"
#include "fx/StutterBufferPass.h"
#include "ofGraphics.h"
#include "ofImage.h"
#include "ofLog.h"
#include "ofUtils.h"

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

        auto addLayerPass = [&](std::unique_ptr<ShaderPass> pass) {
            pass->setLayerId(layer.id);
            runtime->chain.addPass(std::move(pass));
        };

        if (layer.kind == LayerKind::Clip) {
            runtime->player = std::make_unique<ClipPlayer>();
            runtime->loadedPath = layer.resolvedPath;
            if (!layer.resolvedPath.empty()) {
                runtime->player->load(layer.resolvedPath);
                if (!runtime->player->isLoaded()) {
                    runtime->retriesLeft = 3;
                    runtime->nextRetrySecs = ofGetElapsedTimef() + 4.0f;
                }
            } else {
                ofLogWarning("SceneRenderer") << "Scene \"" << scene.name << "\" layer \"" << layer.id
                                              << "\" has no resolved clip -- rendering black.";
            }
        } else {
            // Generator: a paint pass sits FIRST in the chain (it ignores
            // the black seed and fills the layer), so the same effect
            // stack below applies to generated content as to footage.
            runtime->generatorSource = layer.source;
            auto generator = makeGeneratorPass(layer.source);
            if (generator) {
                addLayerPass(std::move(generator));
            } else {
                ofLogWarning("SceneRenderer") << "Scene \"" << scene.name << "\" layer \"" << layer.id
                                              << "\": unknown generator \"" << layer.source
                                              << "\" -- rendering black.";
            }
        }

        // Per-layer effect chain. Order matters: stutter records/loops the
        // RAW source first (so warps keep animating over a held loop),
        // warps resample, palette quantizes last (posterizing after warps
        // avoids banded edges getting smeared by resampling). Idle passes
        // cost nothing -- the chain skips any pass whose isActive() says
        // its params are at neutral.
        addLayerPass(std::make_unique<StutterBufferPass>());
        addLayerPass(std::make_unique<RotozoomPass>());
        addLayerPass(std::make_unique<KaleidoscopePass>());
        addLayerPass(std::make_unique<TwisterBarsPass>());
        addLayerPass(std::make_unique<TunnelPass>());
        addLayerPass(std::make_unique<PosterizeCyclePass>());
        runtimes.push_back(std::move(runtime));
    }
}

bool SceneRenderer::matchesRuntimes(const Scene& scene) const {
    if (scene.layers.size() != runtimes.size()) return false;
    for (size_t i = 0; i < scene.layers.size(); i++) {
        const Layer& layer = scene.layers[i];
        const LayerRuntime& runtime = *runtimes[i];
        if (layer.id != runtime.layerId || layer.kind != runtime.kind) return false;
        if (layer.kind == LayerKind::Clip && layer.resolvedPath != runtime.loadedPath) return false;
        if (layer.kind == LayerKind::Generator && layer.source != runtime.generatorSource) return false;
    }
    return true;
}

void SceneRenderer::update() {
    for (auto& runtime : runtimes) {
        if (!runtime->player) continue;

        if (!runtime->player->isLoaded() && runtime->retriesLeft > 0
            && ofGetElapsedTimef() >= runtime->nextRetrySecs) {
            runtime->retriesLeft--;
            ofLogNotice("SceneRenderer") << "Retrying clip load for layer \"" << runtime->layerId << "\" ("
                                         << runtime->retriesLeft << " retries left): " << runtime->loadedPath;
            runtime->player->load(runtime->loadedPath);
            runtime->nextRetrySecs = ofGetElapsedTimef() + 4.0f;
        }

        runtime->player->update();
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

void SceneRenderer::render(const ControlState& controlState, const LiveParams& liveParams) {
    // Freeze-frame during scene switches: leave the previous output alone
    // until every new clip layer has a real decoded frame to show.
    if (!layersReady() || !liveParams.scene) return;
    const Scene& scene = *liveParams.scene;

    compositor.reset();
    for (const auto& runtime : runtimes) {
        // Blend mode / opacity read fresh each frame (looked up by stable
        // layerId, opacity through the live-param overlay) so mappings and
        // hot-reloaded edits apply without touching the runtime.
        const Layer* layer = nullptr;
        for (const auto& l : scene.layers) {
            if (l.id == runtime->layerId) {
                layer = &l;
                break;
            }
        }
        if (!layer) continue;  // runtime for a layer the scene no longer has

        if (runtime->player && runtime->player->isLoaded()) {
            // Layer transform: contain-fit the clip's native aspect ratio
            // (portrait footage pillarboxes instead of stretching), then
            // user scale and x/y position on top -- all live-mappable.
            // x/y are normalized: ±1 moves the clip's center to the screen
            // edge, so three portrait clips sit side by side at roughly
            // x = -0.6 / 0 / +0.6.
            float texW = runtime->player->getTexture().getWidth();
            float texH = runtime->player->getTexture().getHeight();
            ofRectangle dest(0, 0, width, height);
            if (texW > 0 && texH > 0) {
                float fit = std::min(width / texW, height / texH);
                float scale = fit * liveParams.getLayerParam(layer->id, "transform.scale", 1.0f);
                float w = texW * scale;
                float h = texH * scale;
                float cx = width * 0.5f + liveParams.getLayerParam(layer->id, "transform.x", 0.0f) * width * 0.5f;
                float cy = height * 0.5f + liveParams.getLayerParam(layer->id, "transform.y", 0.0f) * height * 0.5f;
                dest.set(cx - w * 0.5f, cy - h * 0.5f, w, h);
            }
            runtime->chain.process(runtime->player->getDrawable(), dest, controlState, liveParams);
        } else {
            // Generator (its paint pass overwrites the black seed) or
            // unresolved clip (chain has no paint pass: stays black).
            runtime->chain.process(blackFbo, controlState, liveParams);
        }
        float opacity = liveParams.getLayerParam(layer->id, "opacity", layer->opacity);
        compositor.addLayer(runtime->chain.getOutputFbo().getTexture(), layer->blendMode, opacity);
    }

    postChain.process(compositor.getResult(), controlState, liveParams);

    outputFbo.begin();
    ofClear(0, 0, 0, 255);
    ofSetColor(255);
    postChain.getOutputFbo().draw(0, 0, width, height);
    outputFbo.end();

    // Self-dump for headless verification (set LIVEPI_DEBUG_DUMP to a
    // directory): writes the final output every ~2s. How effects get
    // verified on machines whose screen can't be captured.
    static const char* dumpDir = std::getenv("LIVEPI_DEBUG_DUMP");
    if (dumpDir) {
        static int frameCount = 0;
        if (++frameCount % 120 == 0) {
            ofPixels pixels;
            outputFbo.readToPixels(pixels);
            ofSaveImage(pixels, std::string(dumpDir) + "/render-dump.png");
        }
    }
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
            ss << "generator: " << (runtime->generatorSource.empty() ? "?" : runtime->generatorSource);
        }
        if (i + 1 < runtimes.size()) ss << "\n";
    }
    return ss.str();
}
