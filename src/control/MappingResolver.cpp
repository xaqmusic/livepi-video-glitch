#include "MappingResolver.h"

#include "ofMath.h"

namespace {

float bandLevel(const ControlState& state, AudioBandChoice band) {
    switch (band) {
        case AudioBandChoice::Low: return state.lowBand;
        case AudioBandChoice::Mid: return state.midBand;
        case AudioBandChoice::High: return state.highBand;
    }
    return 0.0f;
}

// The static baseline an audio-band contribution rides on when no absolute
// value has been set: the scene's configured param for scene scope, the
// layer's own fields for layer scope, the mapping's min as the neutral
// floor when nothing configures it at all.
float staticBaseline(const Scene& scene, const MappingTarget& target) {
    if (target.layerId.empty()) {
        return scene.getParam(target.param, target.min);
    }
    for (const auto& layer : scene.layers) {
        if (layer.id != target.layerId) continue;
        if (target.param == "opacity") return layer.opacity;
        auto it = layer.layerEffects.find(target.param);
        if (it != layer.layerEffects.end()) return it->second;
        auto pit = layer.params.find(target.param);
        if (pit != layer.params.end()) return pit->second;
        break;
    }
    return target.min;
}

}  // namespace

void MappingResolver::onSceneEnter(const Scene& scene, const std::map<int, float>& ccValues) {
    absoluteStore.clear();
    pendingManualCc.clear();
    prevCcValues = ccValues;

    prevNoteValues.clear();
    pendingManualNote.clear();

    // Seed CC-mapped targets from wherever each knob is currently latched --
    // but only for CCs that have actually been seen; untouched knobs leave
    // the scene's static baselines in effect. Notes deliberately DON'T
    // seed: a note released before the scene switch shouldn't pin its
    // targets at 0 over the scene's own baseline.
    for (const auto& mapping : scene.mappings) {
        if (mapping.trigger.type != TriggerType::CC) continue;
        auto it = ccValues.find(mapping.trigger.number);
        if (it == ccValues.end()) continue;
        applyTriggerValue(scene, TriggerType::CC, mapping.trigger.number, it->second);
    }
}

void MappingResolver::setManualCc(int ccNumber, float value01) {
    pendingManualCc[ccNumber] = ofClamp(value01, 0.0f, 1.0f);
}

void MappingResolver::setManualNote(int noteNumber, float value01) {
    pendingManualNote[noteNumber] = ofClamp(value01, 0.0f, 1.0f);
}

void MappingResolver::setManualParam(const std::string& layerId, const std::string& param, float value) {
    absoluteStore[{layerId, param}] = value;
}

void MappingResolver::applyTriggerValue(const Scene& scene, TriggerType type, int number, float value01) {
    for (const auto& mapping : scene.mappings) {
        if (mapping.trigger.type != type || mapping.trigger.number != number) continue;
        for (const auto& target : mapping.targets) {
            absoluteStore[{target.layerId, target.param}] = ofLerp(target.min, target.max, value01);
        }
    }
}

LiveParams MappingResolver::resolve(const Scene& scene, const ControlState& controlState) {
    // 1. Absolute updates: real CC edges (value changed since last frame,
    //    or a CC seen for the first time) and manual injections.
    for (const auto& [cc, value] : controlState.ccValues) {
        auto prev = prevCcValues.find(cc);
        if (prev == prevCcValues.end() || prev->second != value) {
            applyTriggerValue(scene, TriggerType::CC, cc, value);
        }
    }
    prevCcValues = controlState.ccValues;

    for (const auto& [note, value] : controlState.noteValues) {
        auto prev = prevNoteValues.find(note);
        if (prev == prevNoteValues.end() || prev->second != value) {
            applyTriggerValue(scene, TriggerType::Note, note, value);
        }
    }
    prevNoteValues = controlState.noteValues;

    for (const auto& [cc, value] : pendingManualCc) {
        applyTriggerValue(scene, TriggerType::CC, cc, value);
    }
    pendingManualCc.clear();
    for (const auto& [note, value] : pendingManualNote) {
        applyTriggerValue(scene, TriggerType::Note, note, value);
    }
    pendingManualNote.clear();

    // 2. Base view = the absolute store.
    LiveParams live;
    live.scene = &scene;
    for (const auto& [key, value] : absoluteStore) {
        if (key.first.empty()) {
            live.sceneOverlay[key.second] = value;
        } else {
            live.layerOverlay[key.first][key.second] = value;
        }
    }

    // 3. Audio-band contributions, additive-then-clamped, recomputed fresh
    //    every frame on top of the current base.
    for (const auto& mapping : scene.mappings) {
        if (mapping.trigger.type != TriggerType::AudioBand) continue;
        float band = bandLevel(controlState, mapping.trigger.band);
        for (const auto& target : mapping.targets) {
            TargetKey key{target.layerId, target.param};
            auto stored = absoluteStore.find(key);
            float base = stored != absoluteStore.end() ? stored->second : staticBaseline(scene, target);
            // min/max size the CONTRIBUTION (how much of the band touches
            // the param); the result clamps to the param's 0..1 domain, NOT
            // [min,max] -- clamping to the mapping range would cap a param
            // below its own baseline whenever baseline > max.
            float value = ofClamp(base + band * (target.max - target.min), 0.0f, 1.0f);
            if (target.layerId.empty()) {
                live.sceneOverlay[target.param] = value;
            } else {
                live.layerOverlay[target.layerId][target.param] = value;
            }
        }
    }

    return live;
}
