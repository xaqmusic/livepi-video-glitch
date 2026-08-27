#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "control/ControlState.h"
#include "scenes/LiveParams.h"
#include "fx/LayerCompositor.h"
#include "fx/ShaderChain.h"
#include "fx/ShaderPass.h"
#include "ofFbo.h"
#include "ofImage.h"
#include "scenes/Scene.h"
#include "video/ClipPlayer.h"

// Renders the active scene's full pipeline: each layer plays (or, for
// generators, renders a placeholder) through its own per-layer effect
// chain, the results composite bottom-to-top through LayerCompositor, and
// the composited frame runs the scene-level post chain (the CRT-decay
// passes ofApp registers via addPostPass). getOutputFbo() always holds the
// last successfully rendered frame -- during a scene switch, while the new
// layers' decoders spin up, render() simply doesn't touch it, so the
// switch reads as a brief freeze-frame instead of a black flash.
//
// Layer runtimes exist only for the ACTIVE scene, and loadScene() destroys
// the old ones before creating new ones: decoder sessions all share the
// Pi's single v4l2 block, and the measured budget (docs/architecture.md,
// "Simultaneous clip decode budget") leaves no headroom for overlapping
// old and new scenes' pipelines.
class SceneRenderer {
public:
    void setup(int width, int height);
    void addPostPass(std::unique_ptr<ShaderPass> pass);

    // Change the internal render resolution (the thermal governor drops it to
    // shed GPU heat). Reallocating the pipeline FBOs is a brief hitch. When
    // useTransition is true (and the scene has a transition style) this MASKS it
    // with the scene's transition -- ramp the effect to peak, resize under full
    // cover, ramp back in. When false, or the scene's style is None, it resizes
    // immediately (a short freeze-frame / scene restart). Callers pass false to
    // honor the "no transition on thermal safety" gear-menu option. No-op if
    // already at w x h, or if a transition is already running (caller retries).
    void requestResize(int width, int height, bool useTransition = true);
    int renderWidth() const { return width; }

    // The native display size the per-scene renderScale is a fraction OF, an
    // optional global ceiling (render.scale config baseline), and the live
    // thermal cap. loadScene sizes the internal render to
    // base * min(scene.renderScale, maxScale, thermalScale) as it enters each
    // scene -- so a scene entered while the SoC is hot lands at the reduced
    // resolution under its OWN entry transition, with no second transition after.
    void setBaseSize(int width, int height);
    void setMaxScale(float maxScale);
    // Live thermal cap (1.0 = no cap), pushed every frame before scene loads.
    void setThermalScale(float thermalScale);

    // Seed the output with a splash image instead of black, and hold it there
    // for at least minHoldSecs. Window B of the boot sequence: the renderer has
    // a GL context but no scene ready yet, and on a cold boot it legitimately
    // spends ~16s in the video prewarm. Without this the panel is simply black
    // for that whole stretch. A hold floor is required because on a WARM boot
    // the same window is under a second -- fast enough that an unheld splash is
    // a flash, and perversely shorter the better the box performs.
    // Empty path or an unreadable file = the old opaque-black seed.
    void setSplash(const std::string& imagePath, float minHoldSecs);
    // Hold the splash regardless of the time floor, until the caller says the
    // startup work behind it is done. The floor alone is not enough: the FIRST
    // frame compiles every shader and can itself take seconds, so a 2s floor can
    // expire before the prewarm has even begun and the splash disappears with
    // the slow part still ahead of it.
    void setSplashHold(bool held) { splashExternalHold = held; }

    void loadScene(const Scene& scene);
    void update(const LiveParams& liveParams);
    void render(const ControlState& controlState, const LiveParams& liveParams);

    // True when the scene's layer STRUCTURE (ordered ids, kinds, clip
    // sources) matches the current runtimes -- i.e., a hot-reloaded edit
    // only touched params/mappings and the running players must NOT be
    // rebuilt (no clip restart, no flicker: the seam-aware reload rule).
    bool matchesRuntimes(const Scene& scene) const;

    ofFbo& getOutputFbo() { return outputFbo; }

    // Debug-overlay helpers.
    size_t getLayerCount() const { return runtimes.size(); }
    std::string describeLayers() const;

    // A clip preroll (ofGstVideoPlayer::load) blocks the render thread, and a
    // cold 1080p load on a hot SoC can outrun the frame watchdog. These bracket
    // each such load so the caller (ofApp) can widen the watchdog's grace around
    // it; unset in dev, where there's no watchdog to appease.
    std::function<void(double)> onLoadBegin;  // arg: grace seconds
    std::function<void()> onLoadEnd;

private:
    struct LayerRuntime {
        std::string layerId;
        LayerKind kind = LayerKind::Clip;
        std::string loadedPath;  // resolved clip path this runtime is playing
        std::string generatorSource;  // generator name (kind==Generator); a
                                      // source change must rebuild the chain
        std::unique_ptr<ClipPlayer> player;  // null for generator + image layers
        // A clip-source layer whose file is a still image: a static texture, no
        // decoder. Inferred from the resolved path's extension (isImagePath).
        bool isImage = false;
        ofImage image;
        ShaderChain chain;
        // loadedPath is a baked boomerang (ping-pong reverse baked in): play
        // it whole/forward/looping, no trim enforcement (see ShowLoader).
        bool bakedLoop = false;
        // Clip loads can time out under boot-time contention (GStreamer's
        // preroll racing X/backend/boot tasks for the decoder) -- retry a
        // few times instead of leaving the layer black until a scene
        // change (observed on a real cold boot).
        int retriesLeft = 0;
        float nextRetrySecs = 0.0f;
        float lastSeekSecs = 0.0f;  // debounce for playback-window seeks
    };

    bool layersReady() const;

    // Prerolls a clip through onLoadBegin/onLoadEnd so a slow cold load can't
    // trip the frame watchdog. Grace is generous: a genuine hang still recovers.
    static constexpr float kClipLoadGraceSecs = 30.0f;
    void loadClipGuarded(ClipPlayer& player, const std::string& path);

    // Effect-masked scene transition (docs decision: a crossfade needs two
    // live decode pipelines, which the Pi's budget forbids -- instead the
    // entering scene's transition style ramps an effect to obliteration
    // over the held last frame, holds peak while the new decoders spin
    // up, and ramps back down over the incoming scene).
    struct Transition {
        TransitionSpec spec;       // style None = inactive
        float startSecs = 0.0f;
        bool outDone = false;      // reached peak
        float inStartSecs = -1.0f; // set when layers became ready at peak
    };
    Transition transition;
    bool firstSceneLoaded = false;
    std::string lastLoadedSceneId;
    float transitionValue(float now);
    // Deferred swap: while a transition's OUT phase runs, the OLD scene
    // keeps playing (its runtimes stay alive) and the target waits here;
    // the actual destroy-and-create happens at peak obliteration, so the
    // decoder spin-up is fully covered by the effect.
    std::unique_ptr<Scene> pendingScene;
    Scene renderScene;  // the scene the CURRENT runtimes represent
    void applyScene(const Scene& scene);

    // Deferred internal-resolution change, applied at the transition's peak
    // obliteration (same point as a deferred scene swap) so the FBO realloc is
    // hidden. See requestResize().
    bool pendingResize = false;
    int pendingResizeW = 0;
    int pendingResizeH = 0;
    void applyResize();

    // Native display size + global scale ceiling for per-scene renderScale.
    int baseWidth = 0;
    int baseHeight = 0;
    float maxScale = 1.0f;
    float thermalScale = 1.0f;  // live SoC-heat cap, folded into sceneRenderDims
    // Internal render dimensions for a scene's renderScale (capped by maxScale
    // and the thermal cap), or {0,0} if the base size isn't known yet (leave the
    // size untouched).
    void sceneRenderDims(float sceneScale, int& outW, int& outH) const;

    std::vector<std::unique_ptr<LayerRuntime>> runtimes;
    LayerCompositor compositor;
    ShaderChain postChain;
    ofFbo outputFbo;
    // While true, render() leaves outputFbo alone so the splash stays up. Cleared
    // once the hold has elapsed AND a scene is genuinely ready to replace it.
    bool splashHolding = false;
    bool splashExternalHold = false;
    float splashUntilSecs = 0.0f;
    ofFbo blackFbo;  // seed for generator-placeholder layer chains
    int width = 0;
    int height = 0;
};
