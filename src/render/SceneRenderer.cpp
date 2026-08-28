// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
#include "SceneRenderer.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "fx/FilterPasses.h"
#include "fx/GeneratorPasses.h"
#include "fx/StutterBufferPass.h"
#include "ofAppRunner.h"
#include "ofFileUtils.h"
#include "ofGraphics.h"
#include "ofImage.h"
#include "ofLog.h"
#include "ofUtils.h"
#include "util/DataPath.h"

namespace {
// A clip-source layer whose file is a still image (png/jpg) renders as a static
// texture instead of a decoder session -- see loadScene/render. Same layer kind
// ("clip"), inferred purely from the resolved file extension so nothing in the
// show schema has to distinguish the two.
bool isImagePath(const std::string& path) {
    std::string ext = ofToLower(ofFilePath::getFileExt(path));
    return ext == "png" || ext == "jpg" || ext == "jpeg";
}
}  // namespace

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

void SceneRenderer::loadClipGuarded(ClipPlayer& player, const std::string& path) {
    // ClipPlayer::load prerolls the GStreamer pipeline synchronously on this
    // (render) thread -- a cold 1080p load on a hot, throttling SoC has been
    // seen to exceed the 10s frame watchdog and abort the show. Widen the
    // watchdog's grace across just this call; a real hang still trips after it.
    if (onLoadBegin) onLoadBegin(kClipLoadGraceSecs);
    player.load(path);
    if (onLoadEnd) onLoadEnd();
}

void SceneRenderer::requestResize(int w, int h, bool useTransition) {
    if (w < 16 || h < 16) return;
    if ((w == width && h == height) || pendingResize) return;
    // Don't stomp a scene switch (or a prior resize) already in flight -- the
    // thermal governor re-requests every few seconds, so just wait for idle.
    if (transition.spec.style != TransitionStyle::None) return;

    pendingResizeW = w;
    pendingResizeH = h;
    if (useTransition && renderScene.transition.style != TransitionStyle::None) {
        // Mask the FBO realloc with the scene's own transition: ramp its effect
        // to peak, resize under full cover, ramp back in (render()'s peak block
        // does the actual applyResize, same point a deferred scene swap lands).
        transition.spec = renderScene.transition;
        transition.startSecs = ofGetElapsedTimef();
        transition.outDone = false;
        transition.inStartSecs = -1.0f;
        pendingResize = true;
    } else {
        applyResize();  // no transition style: resize now (brief freeze-frame)
    }
}

void SceneRenderer::applyResize() {
    pendingResize = false;
    if (pendingResizeW == width && pendingResizeH == height) return;
    width = pendingResizeW;
    height = pendingResizeH;

    // Resize the pipeline FBOs IN PLACE -- do NOT rebuild the layer runtimes.
    // Rebuilding (the old applyScene call here) re-prerolled every clip: a
    // blocking GStreamer load on the render thread, fired on EVERY thermal step,
    // exactly when the SoC is hottest and slowest (and a needless one -- a
    // resolution change doesn't change the video). The players keep decoding at
    // native res; only the chain FBOs they draw into change size. Post passes
    // are stateless and layer passes drop their render-sized caches via onResize
    // (ShaderChain::resize), so no pass state goes stale. Clips never respin, so
    // the resize is a seamless in-place reallocation instead of a freeze-frame.
    compositor.setup(width, height);
    postChain.resize(width, height);
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

    for (auto& runtime : runtimes) runtime->chain.resize(width, height);
    ofLogNotice("SceneRenderer") << "internal render resized to " << width << "x" << height;
}

void SceneRenderer::setBaseSize(int w, int h) {
    baseWidth = w;
    baseHeight = h;
}

void SceneRenderer::setMaxScale(float s) {
    maxScale = std::clamp(s, 0.25f, 1.0f);
}

void SceneRenderer::setThermalScale(float s) {
    thermalScale = std::clamp(s, 0.25f, 1.0f);
}

void SceneRenderer::sceneRenderDims(float sceneScale, int& outW, int& outH) const {
    if (baseWidth <= 0 || baseHeight <= 0) {
        outW = 0;
        outH = 0;  // base size unknown -> caller leaves the current size alone
        return;
    }
    const float s = std::min({std::clamp(sceneScale, 0.25f, 1.0f), maxScale, thermalScale});
    outW = std::max(64, static_cast<int>(std::lround(baseWidth * s)));
    outH = std::max(64, static_cast<int>(std::lround(baseHeight * s)));
}

void SceneRenderer::setSplash(const std::string& imagePath, float minHoldSecs) {
    if (imagePath.empty()) return;
    ofImage splash;
    // Quiet on failure: a missing or corrupt splash must never stop the box
    // booting, it just means the old opaque-black seed stays.
    ofSetLogLevel("ofImage", OF_LOG_ERROR);
    if (!splash.load(imagePath)) {
        ofLogWarning("SceneRenderer") << "splash image not loaded: " << imagePath;
        return;
    }
    // Contain-fit and centre, exactly like a layer's own fit: the panel is
    // 1920x720 on one box and 1080p on another, and a splash that stretches or
    // crops would look broken on whichever it wasn't authored for.
    float fit = std::min(static_cast<float>(width) / splash.getWidth(),
                         static_cast<float>(height) / splash.getHeight());
    float w = splash.getWidth() * fit;
    float h = splash.getHeight() * fit;
    outputFbo.begin();
    ofClear(0, 0, 0, 255);
    ofSetColor(255);
    // Alpha-carrying PNGs composite over the black ground rather than punching
    // a transparent hole in it.
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    splash.draw((width - w) * 0.5f, (height - h) * 0.5f, w, h);
    outputFbo.end();

    splashHolding = true;
    splashUntilSecs = ofGetElapsedTimef() + std::max(0.0f, minHoldSecs);
    ofLogNotice("SceneRenderer") << "splash up (" << splash.getWidth() << "x" << splash.getHeight()
                                 << ", hold " << minHoldSecs << "s): " << imagePath;
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
        // Ride the SAME entry transition for this scene's render resolution:
        // queue the resize so render()'s peak block applies it alongside the
        // scene swap (one transition covers both -- no double blink). thermal
        // re-caps it next tick if the SoC is hot.
        int tw = 0, th = 0;
        sceneRenderDims(scene.renderScale, tw, th);
        if (tw > 0 && (tw != width || th != height)) {
            pendingResizeW = tw;
            pendingResizeH = th;
            pendingResize = true;
        }
        lastLoadedSceneId = scene.id;
        ofLogNotice("SceneRenderer") << "transition: style " << static_cast<int>(scene.transition.style)
                                     << " duration " << scene.transition.duration << "s into \"" << scene.name << "\"";
        return;
    }
    firstSceneLoaded = true;
    lastLoadedSceneId = scene.id;
    pendingScene.reset();
    // Immediate entry (first scene, or a None-transition switch): size the
    // internal render to this scene's scale BEFORE building its runtimes, so
    // they allocate at the right resolution -- no separate resize hitch.
    int tw = 0, th = 0;
    sceneRenderDims(scene.renderScale, tw, th);
    if (tw > 0 && (tw != width || th != height)) {
        pendingResizeW = tw;
        pendingResizeH = th;
        applyResize();
    }
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
            runtime->loadedPath = layer.resolvedPath;
            runtime->bakedLoop = layer.bakedLoop;
            if (isImagePath(layer.resolvedPath)) {
                // Still image used as a clip source: a static texture, no
                // decoder/retry machinery. Loads synchronously and is ready at
                // once (layersReady only waits on clip players).
                runtime->isImage = true;
                if (!runtime->image.load(livepi::userDataPath(layer.resolvedPath))) {
                    ofLogWarning("SceneRenderer") << "Scene \"" << scene.name << "\" layer \"" << layer.id
                                                  << "\": could not load image " << layer.resolvedPath;
                } else if (runtime->image.getPixels().getNumChannels() == 4) {
                    // Cut-out PNG: carry its alpha through the chain so the
                    // compositor blends it over the layers below instead of
                    // showing a black box where the image is transparent.
                    runtime->chain.setPreserveSourceAlpha(true);
                }
            } else if (!layer.resolvedPath.empty()) {
                runtime->player = std::make_unique<ClipPlayer>();
                loadClipGuarded(*runtime->player, layer.resolvedPath);
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
        // Kaleidoscope before rotozoom: mirror the source into wedges first,
        // then spin/zoom the kaleidoscoped image as a whole.
        addLayerPass(std::make_unique<KaleidoscopePass>());
        addLayerPass(std::make_unique<RotozoomPass>());
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
        // Trails LAST: it echoes whatever the whole layer chain produced, so
        // its feedback buffer carries the finished look into its own wake.
        addLayerPass(std::make_unique<TrailsPass>());
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
        if (layer.kind == LayerKind::Clip && layer.bakedLoop != runtime.bakedLoop) return false;
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
            loadClipGuarded(*runtime->player, runtime->loadedPath);
            runtime->nextRetrySecs = ofGetElapsedTimef() + 4.0f;
        }

        runtime->player->update();
        if (!runtime->player->isLoaded()) continue;

        // Baked boomerang: forward + reverse are already one file. Just let
        // it loop forward -- no trim, no ping-pong logic, no backwards decode.
        // This is how ping-pong plays on the Pi (its v4l2 decoder stalls on
        // rate -1), and identically on the desktop.
        if (runtime->bakedLoop) continue;

        // Plain clip: enforce the playback window (video.start/end) forward
        // only. Ping-pong's reverse leg needs a baked boomerang (ShowLoader
        // swaps this layer to one when video.pingpong is on and a matching
        // file exists); with the toggle on but nothing baked yet, we simply
        // loop the trimmed segment forward until it's prepped. Seeks are
        // debounced -- a GStreamer seek is async, and re-issuing every frame
        // while position catches up turns the loop point into a stall.
        float now = ofGetElapsedTimef();
        if (now - runtime->lastSeekSecs < 0.25f) continue;
        float start = std::clamp(liveParams.getLayerParam(runtime->layerId, "video.start", 0.0f), 0.0f, 0.95f);
        float end = std::clamp(liveParams.getLayerParam(runtime->layerId, "video.end", 1.0f), start + 0.02f, 1.0f);
        float pos = runtime->player->getPosition();
        // Only enforce when actually trimmed -- at the full window the
        // player's own OF_LOOP_NORMAL wrap is seamless and shouldn't be
        // second-guessed (pos briefly reads 1.0 at the wrap).
        bool trimmed = start > 0.001f || end < 0.999f;
        if (trimmed && (pos >= end || pos < start - 0.01f)) {
            runtime->player->setPosition(start);
            runtime->lastSeekSecs = now;
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
    if (splashHolding) {
        // Leave outputFbo alone -- the same mechanism that makes a scene switch
        // a freeze-frame instead of a black flash. Two conditions, not one: the
        // hold floor keeps it visible on a fast boot, and layersReady() keeps it
        // up on a slow one until there is genuinely something to replace it with.
        if (splashExternalHold || ofGetElapsedTimef() < splashUntilSecs || !layersReady()) return;
        splashHolding = false;
        ofLogNotice("SceneRenderer") << "splash released";
    }
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
    if (transition.outDone && pendingResize) {
        // Peak obliteration: swap the render resolution under full cover, just
        // like the scene swap above. layersReady() then holds the peak while
        // any clips respin, and the in-ramp dissolves back over the new size.
        applyResize();
    }
    if (tv > 0.0f) {
        switch (transition.spec.style) {
            case TransitionStyle::Tear:
                liveParams.sceneOverlay["hsync.intensity"] =
                    std::max(liveParams.getParam("hsync.intensity", 0.5f), tv);
                break;
            case TransitionStyle::Static:
                // Ramp the snow to a full dead-channel field at the peak, so
                // the scene dissolves into static and the incoming one tunes
                // back in. Static is the first post pass, idle-skipped when 0.
                liveParams.sceneOverlay["static.amount"] =
                    std::max(liveParams.getParam("static.amount", 0.0f), tv);
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

        // A clip player's planar frame or a still image's texture -- both feed
        // the same contain-fit transform and effect chain.
        const ofBaseDraws* source = nullptr;
        float texW = 0.0f, texH = 0.0f;
        if (runtime->player && runtime->player->isLoaded()) {
            source = &runtime->player->getDrawable();
            texW = runtime->player->getTexture().getWidth();
            texH = runtime->player->getTexture().getHeight();
        } else if (runtime->isImage && runtime->image.isAllocated()) {
            source = &runtime->image.getTexture();
            texW = runtime->image.getWidth();
            texH = runtime->image.getHeight();
        }

        if (source) {
            // Layer transform: contain-fit the source's native aspect ratio
            // (portrait footage pillarboxes instead of stretching), then
            // user scale and x/y position on top -- all live-mappable.
            // x/y are normalized: ±1 moves the source's center to the screen
            // edge, so three portrait clips sit side by side at roughly
            // x = -0.6 / 0 / +0.6.
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
            float rotation = liveParams.getLayerParam(layer->id, "transform.rotation", 0.0f);
            runtime->chain.process(*source, dest, controlState, liveParams, rotation);
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
               << runtime->player->getPixelFormatName() << (runtime->bakedLoop ? " ><" : "") << "  pos: "
               << (runtime->player->getPosition() * runtime->player->getDuration()) << "s /"
               << runtime->player->getDuration() << "s";
        } else {
            ss << "generator: " << (runtime->generatorSource.empty() ? "?" : runtime->generatorSource);
        }
        if (i + 1 < runtimes.size()) ss << "\n";
    }
    return ss.str();
}
