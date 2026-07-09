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
    void update();
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
        std::unique_ptr<ClipPlayer> player;  // null for generator layers
        ShaderChain chain;
    };

    bool layersReady() const;

    std::vector<std::unique_ptr<LayerRuntime>> runtimes;
    LayerCompositor compositor;
    ShaderChain postChain;
    ofFbo outputFbo;
    ofFbo blackFbo;  // seed for generator-placeholder layer chains
    int width = 0;
    int height = 0;
};
