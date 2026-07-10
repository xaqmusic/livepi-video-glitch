#include "SceneRenderer.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "fx/FilterPasses.h"
#include "fx/GeneratorPasses.h"
#include "fx/StutterBufferPass.h"
#include "ofAppRunner.h"
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
    // A real switch (different scene id) with a transition style DEFERS
    // the swap: the old scene keeps playing while the OUT ramp rises over
    // LIVE video, and the destroy-and-create happens at peak obliteration
    // (hot-reload rebuilds reuse the same id and stay immediate).
    if (firstSceneLoaded && scene.id != lastLoadedSceneId
        && scene.transition.style != TransitionStyle::None) {
        bool alreadyRamping = transition.spec.style != TransitionStyle::None && !transition.outDone;
        transition.spec = scene.transition;
        if (!alreadyRamping) {
            transition.startSecs = ofGetElapsedTimef();
        }  // else: keep the running ramp's clock -- fast scene surfing just retargets
        transition.outDone = false;
        transition.inStartSecs = -1.0f;
        pendingScene = std::make_unique<Scene>(scene);
        lastLoadedSceneId = scene.id;
        ofLogNotice("SceneRenderer") << "transition: style " << static_cast<int>(scene.transition.style)
                                     << " duration " << scene.transition.duration << "s into \"" << scene.name << "\"";
        return;
    }
    firstSceneLoaded = true;
    lastLoadedSceneId = scene.id;
    pendingScene.reset();
    applyScene(scene);
}

void SceneRenderer::applyScene(const Scene& scene) {
    renderScene = scene;

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
        // Fracture last of the warps: it shatters whatever the layer looks
        // like by then, and its transparent cracks must survive to the
        // compositor (color adjust passes alpha through).
        addLayerPass(std::make_unique<FracturePass>());
        // Color-correct the (possibly warped) source, THEN posterize: the
        // quantizer bins whatever contrast/saturation hands it.
        addLayerPass(std::make_unique<ColorAdjustPass>());
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

void SceneRenderer::update(const LiveParams& liveParams) {
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

        // Playback window (video.start/end) + ping-pong loop mode, all
        // live-mappable layer params. Seeks are debounced: a seek is
        // asynchronous on the GStreamer side, and re-issuing every frame
        // while position catches up turns the loop point into a stall.
        if (!runtime->player->isLoaded()) continue;
        float start = std::clamp(liveParams.getLayerParam(runtime->layerId, "video.start", 0.0f), 0.0f, 0.95f);
        float end = std::clamp(liveParams.getLayerParam(runtime->layerId, "video.end", 1.0f), start + 0.02f, 1.0f);
        bool pingpong = liveParams.getLayerParam(runtime->layerId, "video.pingpong", 0.0f) > 0.5f;
        float now = ofGetElapsedTimef();

        // Active reverse-scrub runs on its own per-frame clock, ahead of
        // the seek debounce (which exists for the coarse wrap seeks, not
        // this deliberately rapid stepping).
        if (runtime->scrubbing) {
            if (!pingpong) {
                // Toggle flipped off mid-scrub: resume normal playback.
                runtime->scrubbing = false;
                runtime->player->setPaused(false);
                runtime->player->setSpeed(1.0f);
                continue;
            }
            float durSecs = std::max(runtime->player->getDuration(), 0.1f);
            runtime->scrubPos -= static_cast<float>(ofGetLastFrameTime()) / durSecs;
            if (runtime->scrubPos <= start) {
                runtime->scrubbing = false;
                runtime->player->setPosition(start);
                runtime->player->setPaused(false);
                runtime->player->setSpeed(1.0f);
                runtime->lastSeekSecs = now;
                ofLogNotice("SceneRenderer") << "ping-pong: forward layer \"" << runtime->layerId
                                             << "\" at pos " << runtime->scrubPos;
            } else if (now - runtime->lastScrubSeek >= 0.04f) {
                // ~25 steps/sec; on an all-intra clip each seek decodes
                // exactly one frame, so the paused pipeline keeps up.
                runtime->player->setPosition(runtime->scrubPos);
                runtime->lastScrubSeek = now;
            }
            continue;
        }

        if (now - runtime->lastSeekSecs < 0.25f) continue;
        float pos = runtime->player->getPosition();
        float speed = runtime->player->getSpeed();

        if (pingpong) {
            // Flip a little before the native end so OF_LOOP_NORMAL's own
            // wrap never races the reversal.
            float flipEnd = std::min(end, 0.99f);
            if (speed >= 0.0f && pos >= flipEnd) {
                if (reverseScrub) {
                    // Hardware path: rate -1 stalls the v4l2 decoder, so
                    // reverse is seek-stepping over a paused pipeline.
                    runtime->scrubbing = true;
                    runtime->scrubPos = flipEnd;
                    runtime->lastScrubSeek = 0.0f;
                    runtime->player->setPaused(true);
                    ofLogNotice("SceneRenderer") << "ping-pong: reversing (scrub) layer \"" << runtime->layerId
                                                 << "\" at pos " << pos;
                } else {
                    runtime->player->setSpeed(-1.0f);
                    ofLogNotice("SceneRenderer") << "ping-pong: reversing layer \"" << runtime->layerId
                                                 << "\" at pos " << pos;
                }
                runtime->lastSeekSecs = now;
            } else if (!reverseScrub && speed < 0.0f && pos <= start) {
                runtime->player->setSpeed(1.0f);
                runtime->lastSeekSecs = now;
                ofLogNotice("SceneRenderer") << "ping-pong: forward layer \"" << runtime->layerId
                                             << "\" at pos " << pos;
            }
        } else {
            if (speed < 0.0f) {
                runtime->player->setSpeed(1.0f);
                runtime->lastSeekSecs = now;
            }
            // Only enforce when actually trimmed -- at the full window the
            // player's own OF_LOOP_NORMAL wrap is seamless and shouldn't
            // be second-guessed (pos briefly reads 1.0 at the wrap).
            bool trimmed = start > 0.001f || end < 0.999f;
            if (trimmed && (pos >= end || pos < start - 0.01f)) {
                runtime->player->setPosition(start);
                runtime->lastSeekSecs = now;
            }
        }
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

namespace {
float smoothRamp(float t) { return t * t * (3.0f - 2.0f * t); }
}  // namespace

float SceneRenderer::transitionValue(float now) {
    if (transition.spec.style == TransitionStyle::None) return 0.0f;
    float outDur = std::max(0.15f, transition.spec.duration * 0.4f);
    float inDur = std::max(0.2f, transition.spec.duration * 0.6f);
    if (!transition.outDone) {
        float t = (now - transition.startSecs) / outDur;
        if (t < 1.0f) return smoothRamp(t);
        transition.outDone = true;
    }
    if (transition.inStartSecs < 0.0f) {
        if (!layersReady()) return 1.0f;  // hold obliteration while decoders spin up
        transition.inStartSecs = now;
    }
    float t = (now - transition.inStartSecs) / inDur;
    if (t >= 1.0f) {
        transition.spec.style = TransitionStyle::None;
        return 0.0f;
    }
    return smoothRamp(1.0f - t);
}

void SceneRenderer::render(const ControlState& controlState, const LiveParams& liveParamsIn) {
    // Transition ramp injects into a COPY of the frame's params -- the
    // resolver's own state is never touched.
    LiveParams liveParams = liveParamsIn;
    // During a deferred swap's OUT phase the resolver already points at
    // the TARGET scene, but the screen still shows the old one: statics
    // (opacity, transforms) must keep coming from the scene the runtimes
    // actually represent.
    if (pendingScene) liveParams.scene = &renderScene;
    float tv = transitionValue(ofGetElapsedTimef());
    if (transition.outDone && pendingScene) {
        // Peak obliteration reached: do the real destroy-and-create under
        // full cover. From here the incoming scene's params apply.
        applyScene(*pendingScene);
        pendingScene.reset();
        liveParams.scene = liveParamsIn.scene;
    }
    if (tv > 0.0f) {
        switch (transition.spec.style) {
            case TransitionStyle::Tear:
                liveParams.sceneOverlay["hsync.intensity"] =
                    std::max(liveParams.getParam("hsync.intensity", 0.5f), tv);
                break;
            case TransitionStyle::Fade:
                liveParams.sceneOverlay["transition.fade"] = tv;
                break;
            case TransitionStyle::Shatter:
                // The dispersal tail makes amount 1 pure void: the old
                // scene breaks apart completely, the new one reassembles.
                liveParams.sceneOverlay["fracture.amount"] = tv;
                break;
            default:
                break;
        }
    }

    // Freeze-frame during scene switches: leave the previous output alone
    // until every new clip layer has a real decoded frame to show --
    // UNLESS a transition is ramping, which re-post-processes the held
    // composite each frame so the obliteration animates over the freeze.
    if (!layersReady() || !liveParams.scene) {
        if (tv > 0.0f && liveParams.scene) {
            postChain.process(compositor.getResult(), controlState, liveParams);
            outputFbo.begin();
            ofClear(0, 0, 0, 255);
            ofSetColor(255);
            postChain.getOutputFbo().draw(0, 0, width, height);
            outputFbo.end();
        }
        return;
    }
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
                // Flips are free: draw with a negative dimension from the
                // opposite edge. Live-mappable toggles like everything else
                // (a note bound to flipH = strobe-mirror on key hits).
                if (liveParams.getLayerParam(layer->id, "transform.flipH", 0.0f) > 0.5f) {
                    dest.x += dest.width;
                    dest.width = -dest.width;
                }
                if (liveParams.getLayerParam(layer->id, "transform.flipV", 0.0f) > 0.5f) {
                    dest.y += dest.height;
                    dest.height = -dest.height;
                }
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
    // Two-decimal floats to match the debug overlay's fixed line widths.
    ss << std::fixed << std::setprecision(2);
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
