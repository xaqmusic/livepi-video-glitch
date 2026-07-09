#include "ShowLoader.h"

#include "ofFileUtils.h"
#include "ofJson.h"
#include "ofLog.h"
#include "ofUtils.h"

namespace {

constexpr int kSupportedSchemaVersion = 1;

BlendMode parseBlendMode(const std::string& s) {
    if (s == "normal") return BlendMode::Normal;
    if (s == "add") return BlendMode::Add;
    if (s == "screen") return BlendMode::Screen;
    if (s == "multiply") return BlendMode::Multiply;
    ofLogWarning("ShowLoader") << "Unknown blend mode \"" << s << "\", using normal.";
    return BlendMode::Normal;
}

// The JSON spells scene-scope targets "postEffects.hsync.intensity"; passes
// read the bare "hsync.intensity" key, so strip the scope prefix here once
// rather than in the per-frame resolver.
std::string stripPostEffectsPrefix(const std::string& param) {
    static const std::string prefix = "postEffects.";
    if (param.rfind(prefix, 0) == 0) return param.substr(prefix.size());
    return param;
}

std::map<std::string, float> parseParamMap(const ofJson& node) {
    std::map<std::string, float> params;
    for (const auto& [key, value] : node.items()) {
        params[key] = value.get<float>();
    }
    return params;
}

}  // namespace

bool ShowLoader::load() {
    std::string activePath = ofToDataPath("shows/active.json", true);
    if (!ofFile::doesFileExist(activePath)) {
        ofLogWarning("ShowLoader") << "shows/active.json not found -- no show loaded.";
        return false;
    }

    ofJson active = ofLoadJson(activePath);
    std::string showName = active.value("activeShow", std::string(""));
    if (showName.empty()) {
        ofLogError("ShowLoader") << "shows/active.json has no \"activeShow\" -- no show loaded.";
        return false;
    }

    std::string showPath = ofToDataPath("shows/" + showName + ".json", true);
    if (!ofFile::doesFileExist(showPath)) {
        ofLogError("ShowLoader") << "Active show file missing: " << showPath;
        return false;
    }

    if (!parseShowFile(showPath)) return false;

    activeShowName = showName;
    ofLogNotice("ShowLoader") << "Loaded show \"" << showName << "\" (" << scenes.size() << " scenes)";
    return true;
}

bool ShowLoader::parseShowFile(const std::string& absPath) {
    ofJson show;
    try {
        show = ofLoadJson(absPath);
    } catch (const std::exception& e) {
        ofLogError("ShowLoader") << "Failed to parse " << absPath << ": " << e.what() << " -- keeping last-good show.";
        return false;
    }
    if (show.is_null() || show.empty()) {
        ofLogError("ShowLoader") << "Empty/unparseable show file: " << absPath << " -- keeping last-good show.";
        return false;
    }

    int version = show.value("schemaVersion", 0);
    if (version != kSupportedSchemaVersion) {
        ofLogError("ShowLoader") << "Show schemaVersion " << version << " unsupported (want "
                                 << kSupportedSchemaVersion << ") -- keeping last-good show.";
        return false;
    }

    auto clipPaths = loadClipLibrary();

    std::vector<Scene> parsed;
    for (const auto& sceneNode : show.value("scenes", ofJson::array())) {
        Scene scene;
        scene.id = sceneNode.value("id", std::string(""));
        scene.name = sceneNode.value("name", std::string("scene"));

        for (const auto& layerNode : sceneNode.value("layers", ofJson::array())) {
            Layer layer;
            layer.id = layerNode.value("id", std::string(""));
            layer.kind = layerNode.value("kind", std::string("clip")) == "generator" ? LayerKind::Generator
                                                                                     : LayerKind::Clip;
            layer.source = layerNode.value("source", std::string(""));
            layer.blendMode = parseBlendMode(layerNode.value("blendMode", std::string("normal")));
            layer.opacity = layerNode.value("opacity", 1.0f);
            if (layerNode.contains("layerEffects")) layer.layerEffects = parseParamMap(layerNode.at("layerEffects"));
            if (layerNode.contains("params")) layer.params = parseParamMap(layerNode.at("params"));

            if (layer.kind == LayerKind::Clip) {
                auto it = clipPaths.find(layer.source);
                if (it != clipPaths.end()) {
                    layer.resolvedPath = it->second;
                } else {
                    ofLogError("ShowLoader") << "Scene \"" << scene.name << "\" layer \"" << layer.id
                                             << "\": clipId \"" << layer.source
                                             << "\" not in clips/library.json -- layer will render black.";
                }
            }
            scene.layers.push_back(std::move(layer));
        }

        for (const auto& mappingNode : sceneNode.value("mappings", ofJson::array())) {
            Mapping mapping;
            const auto& triggerNode = mappingNode.value("trigger", ofJson::object());
            std::string type = triggerNode.value("type", std::string(""));
            if (type == "cc") {
                mapping.trigger.type = TriggerType::CC;
                mapping.trigger.ccNumber = triggerNode.value("number", 0);
            } else if (type == "audioBand") {
                mapping.trigger.type = TriggerType::AudioBand;
                std::string band = triggerNode.value("band", std::string("low"));
                mapping.trigger.band = band == "high"  ? AudioBandChoice::High
                                       : band == "mid" ? AudioBandChoice::Mid
                                                       : AudioBandChoice::Low;
            } else {
                ofLogWarning("ShowLoader") << "Scene \"" << scene.name << "\": unknown mapping trigger type \""
                                           << type << "\" -- mapping skipped.";
                continue;
            }

            for (const auto& targetNode : mappingNode.value("targets", ofJson::array())) {
                MappingTarget target;
                target.layerId = targetNode.value("layerId", std::string(""));
                target.param = targetNode.value("param", std::string(""));
                if (target.layerId.empty()) target.param = stripPostEffectsPrefix(target.param);
                target.min = targetNode.value("min", 0.0f);
                target.max = targetNode.value("max", 1.0f);
                mapping.targets.push_back(std::move(target));
            }
            scene.mappings.push_back(std::move(mapping));
        }

        if (sceneNode.contains("postEffects")) scene.params = parseParamMap(sceneNode.at("postEffects"));

        // Transitional single-clip bridge until the layered SceneRenderer
        // lands (Phase A.2): the first resolvable clip layer is what the
        // current render path plays.
        for (const auto& layer : scene.layers) {
            if (layer.kind == LayerKind::Clip && !layer.resolvedPath.empty()) {
                scene.clipPath = layer.resolvedPath;
                break;
            }
        }

        parsed.push_back(std::move(scene));
    }

    scenes = std::move(parsed);
    return true;
}

std::map<std::string, std::string> ShowLoader::loadClipLibrary() const {
    std::map<std::string, std::string> paths;
    std::string libPath = ofToDataPath("clips/library.json", true);
    if (!ofFile::doesFileExist(libPath)) {
        ofLogWarning("ShowLoader") << "clips/library.json not found -- clip layers won't resolve.";
        return paths;
    }
    ofJson lib = ofLoadJson(libPath);
    for (const auto& clipNode : lib.value("clips", ofJson::array())) {
        std::string id = clipNode.value("id", std::string(""));
        std::string path = clipNode.value("path", std::string(""));
        if (!id.empty() && !path.empty()) paths[id] = path;
    }
    return paths;
}
