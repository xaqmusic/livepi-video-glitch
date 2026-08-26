#include "util/ParamDomains.h"

#include "ofFileUtils.h"
#include "ofJson.h"
#include "ofLog.h"
#include "ofUtils.h"

namespace {

// Pull {"min":..,"max":..} entries out of one manifest object.
void collect(const ofJson& node, std::map<std::string, std::pair<float, float>>& out) {
    if (!node.is_object()) return;
    for (auto it = node.begin(); it != node.end(); ++it) {
        const ofJson& spec = it.value();
        if (!spec.is_object() || !spec.contains("min") || !spec.contains("max")) continue;
        out[it.key()] = {spec.at("min").get<float>(), spec.at("max").get<float>()};
    }
}

}  // namespace

void ParamDomains::load() {
    if (loaded) return;
    loaded = true;  // set first: a failed load must not retry every frame

    // The manifest lives with the backend, one level above bin/data in the app
    // tree -- the same relative layout on the desktop and on the appliance
    // (/data/app/current/backend/...).
    const std::string path = ofToDataPath("../../backend/effects_manifest.json", true);
    if (!ofFile::doesFileExist(path)) {
        ofLogWarning("ParamDomains")
            << "no manifest at " << path << " -- mapping contributions will not be domain-clamped";
        return;
    }

    ofJson manifest;
    try {
        manifest = ofLoadJson(path);
    } catch (const std::exception& e) {
        ofLogWarning("ParamDomains") << "could not parse " << path << ": " << e.what();
        return;
    }

    std::map<std::string, std::pair<float, float>> layer;
    std::map<std::string, std::pair<float, float>> post;
    if (manifest.contains("layerEffects")) collect(manifest.at("layerEffects"), layer);
    if (manifest.contains("postEffects")) collect(manifest.at("postEffects"), post);
    // Generator params are layer-scope too (a generator layer's own controls).
    if (manifest.contains("generators") && manifest.at("generators").is_object()) {
        for (const auto& gen : manifest.at("generators")) {
            if (gen.is_object() && gen.contains("params")) collect(gen.at("params"), layer);
        }
    }

    for (const auto& [k, v] : layer) layerDomains[k] = {v.first, v.second};
    for (const auto& [k, v] : post) postDomains[k] = {v.first, v.second};
    // Layer opacity is mappable but is a Layer FIELD, not a manifest entry --
    // the web UI hardcodes its 0..1 spec, so mirror that here.
    layerDomains["opacity"] = {0.0f, 1.0f};

    ofLogNotice("ParamDomains") << "loaded " << layerDomains.size() << " layer + " << postDomains.size()
                                << " post param domains from " << path;
}

bool ParamDomains::get(bool layerScope, const std::string& param, float& outMin, float& outMax) const {
    const auto& table = layerScope ? layerDomains : postDomains;
    auto it = table.find(param);
    if (it == table.end()) return false;
    outMin = it->second.min;
    outMax = it->second.max;
    return true;
}
