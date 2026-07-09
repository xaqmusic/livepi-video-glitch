#pragma once

#include <map>
#include <string>

#include "Scene.h"

// The per-frame parameter view passes and the compositor read: the mapping
// resolver's live values (CC absolutes, audio-band contributions) overlaid
// on the scene's static baselines. Rebuilt by MappingResolver::resolve()
// every frame -- cheap (a couple of small maps), and it means a scene
// switch or show hot-reload needs no invalidation anywhere.
struct LiveParams {
    const Scene* scene = nullptr;
    std::map<std::string, float> sceneOverlay;                          // post-effect params
    std::map<std::string, std::map<std::string, float>> layerOverlay;   // layerId -> param -> value

    // Scene-scope param: live overlay first, scene static baseline second,
    // caller's fallback (the pass's built-in default) last.
    float getParam(const std::string& key, float fallback) const {
        auto it = sceneOverlay.find(key);
        if (it != sceneOverlay.end()) return it->second;
        return scene ? scene->getParam(key, fallback) : fallback;
    }

    // Layer-scope param: live overlay first, then the layer's own statics
    // (opacity field, layerEffects, generator params), then the caller's
    // fallback (the reading pass's built-in default).
    float getLayerParam(const std::string& layerId, const std::string& key, float fallback) const {
        auto layerIt = layerOverlay.find(layerId);
        if (layerIt != layerOverlay.end()) {
            auto it = layerIt->second.find(key);
            if (it != layerIt->second.end()) return it->second;
        }
        if (scene) {
            for (const auto& layer : scene->layers) {
                if (layer.id != layerId) continue;
                if (key == "opacity") return layer.opacity;
                auto it = layer.layerEffects.find(key);
                if (it != layer.layerEffects.end()) return it->second;
                auto pit = layer.params.find(key);
                if (pit != layer.params.end()) return pit->second;
                break;
            }
        }
        return fallback;
    }
};
