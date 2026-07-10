#pragma once

#include <memory>
#include <string>
#include <vector>

#include "control/ControlState.h"
#include "scenes/LiveParams.h"
#include "fx/LayerCompositor.h"
#include "fx/ShaderChain.h"
#include "fx/ShaderPass.h"
#include "ofFbo.h"
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

    void loadScene(const Scene& scene);
    void update(const LiveParams& liveParams);
    // Ping-pong reverse strategy: true = seek-stepping scrub (hardware
    // decoders without rate -1 support: the Pi), false = native negative
    // rate (desktop). Set from config at startup.
    void setReverseScrub(bool scrub) { reverseScrub = scrub; }
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

private:
    struct LayerRuntime {
        std::string layerId;
        LayerKind kind = LayerKind::Clip;
        std::string loadedPath;  // resolved clip path this runtime is playing
        std::string generatorSource;  // generator name (kind==Generator); a
                                      // source change must rebuild the chain
        std::unique_ptr<ClipPlayer> player;  // null for generator layers
        ShaderChain chain;
        // Clip loads can time out under boot-time contention (GStreamer's
        // preroll racing X/backend/boot tasks for the decoder) -- retry a
        // few times instead of leaving the layer black until a scene
        // change (observed on a real cold boot).
        int retriesLeft = 0;
        float nextRetrySecs = 0.0f;
        float lastSeekSecs = 0.0f;  // debounce for playback-window seeks
        // Reverse-by-scrubbing state (ping-pong on hardware decoders that
        // can't play rate -1): pipeline paused, position walked backward
        // by rapid seeks. scrubPos is OUR clock -- getPosition() lags
        // mid-flush and can't be trusted while scrubbing.
        bool scrubbing = false;
        float scrubPos = 0.0f;
        float lastScrubSeek = 0.0f;
    };

    bool layersReady() const;

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

    bool reverseScrub = false;

    std::vector<std::unique_ptr<LayerRuntime>> runtimes;
    LayerCompositor compositor;
    ShaderChain postChain;
    ofFbo outputFbo;
    ofFbo blackFbo;  // seed for generator-placeholder layer chains
    int width = 0;
    int height = 0;
};
