// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
#include "MappingResolver.h"

#include <algorithm>

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
    (void)scene;  // contributions are recomputed per frame; nothing scene-shaped to seed
    absoluteStore.clear();
    prevCcValues = ccValues;
    prevNoteValues.clear();

    // Adopt the knobs' CURRENT physical positions so entering a scene picks up
    // the hardware where it actually sits rather than snapping to zero. Notes
    // deliberately don't latch: a note released before the switch shouldn't
    // hold its targets down in the new scene.
    ccLatched = ccValues;
    noteLatched.clear();

    domains.load();  // idempotent; first scene entry pays for it
}

void MappingResolver::setManualCc(int ccNumber, float value01) {
    ccLatched[ccNumber] = ofClamp(value01, 0.0f, 1.0f);
}

void MappingResolver::setManualNote(int noteNumber, float value01) {
    noteLatched[noteNumber] = ofClamp(value01, 0.0f, 1.0f);
}

void MappingResolver::setManualParam(const std::string& layerId, const std::string& param, float value) {
    absoluteStore[{layerId, param}] = value;
}

LiveParams MappingResolver::resolve(const Scene& scene, const ControlState& controlState) {
    // 1. Latch input levels. A CC/note only updates on a real edge (changed
    //    since last frame, or seen for the first time), so a browser injection
    //    isn't immediately stomped back by an unchanging physical knob.
    for (const auto& [cc, value] : controlState.ccValues) {
        auto prev = prevCcValues.find(cc);
        if (prev == prevCcValues.end() || prev->second != value) ccLatched[cc] = value;
    }
    prevCcValues = controlState.ccValues;

    for (const auto& [note, value] : controlState.noteValues) {
        auto prev = prevNoteValues.find(note);
        if (prev == prevNoteValues.end() || prev->second != value) noteLatched[note] = value;
    }
    prevNoteValues = controlState.noteValues;

    // 2. Base view = the operator's own manual param pins.
    LiveParams live;
    live.scene = &scene;
    for (const auto& [key, value] : absoluteStore) {
        if (key.first.empty()) {
            live.sceneOverlay[key.second] = value;
        } else {
            live.layerOverlay[key.first][key.second] = value;
        }
    }

    // 3. Sum every mapping's contribution per target, then apply once. Summing
    //    first matters when a param carries BOTH a knob and an audio band:
    //    they stack additively instead of the later one overwriting the earlier.
    std::map<TargetKey, float> contributions;
    for (const auto& mapping : scene.mappings) {
        float level = 0.0f;
        switch (mapping.trigger.type) {
            case TriggerType::CC: {
                auto it = ccLatched.find(mapping.trigger.number);
                if (it == ccLatched.end()) continue;  // knob never touched -- no contribution
                level = it->second;
                break;
            }
            case TriggerType::Note: {
                auto it = noteLatched.find(mapping.trigger.number);
                if (it == noteLatched.end()) continue;
                level = it->second;
                break;
            }
            case TriggerType::AudioBand:
                level = bandLevel(controlState, mapping.trigger.band);
                break;
            default:
                continue;
        }
        // min/max size the CONTRIBUTION, not the result: the span IS the
        // binding's "amount" (a negative span inverts the response, which is
        // how turning a knob up drives a param DOWN from the slider setting).
        for (const auto& target : mapping.targets) {
            contributions[{target.layerId, target.param}] += level * (target.max - target.min);
        }
    }

    // 4. baseline + contribution, clamped to the param's REAL domain.
    for (const auto& [key, contribution] : contributions) {
        const bool layerScope = !key.first.empty();
        MappingTarget probe;
        probe.layerId = key.first;
        probe.param = key.second;
        probe.min = 0.0f;
        auto stored = absoluteStore.find(key);
        float base = stored != absoluteStore.end() ? stored->second : staticBaseline(scene, probe);

        float value = base + contribution;
        float lo = 0.0f, hi = 1.0f;
        if (domains.get(layerScope, key.second, lo, hi)) {
            value = ofClamp(value, lo, hi);
        } else {
            // Domain unknown (manifest missing, or a param it doesn't describe):
            // guard only against the baseline and the contribution's own reach,
            // never an invented 0..1 -- that assumption is exactly what used to
            // pin signed params to zero.
            float reach = base + contribution;
            value = ofClamp(value, std::min(base, reach), std::max(base, reach));
        }

        if (layerScope) {
            live.layerOverlay[key.first][key.second] = value;
        } else {
            live.sceneOverlay[key.second] = value;
        }
    }

    return live;
}
