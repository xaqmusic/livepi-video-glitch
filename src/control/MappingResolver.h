#pragma once

#include <map>
#include <string>
#include <utility>

#include "ControlState.h"
#include "scenes/LiveParams.h"
#include "scenes/Scene.h"

// The per-frame step that turns the active scene's mapping table plus the
// live inputs (MIDI CCs, audio-band envelopes, browser commands) into the
// LiveParams view everything downstream reads. Semantics per
// docs/videosynth-backend.md:
//
// - CC triggers are ABSOLUTE: the resolved value IS the target's value,
//   like a hardware knob. Held in a per-scene store updated only when the
//   CC actually changes (edge-detected against the previous frame) or a
//   manual command arrives -- last writer wins, so a phone slider and a
//   physical knob contend exactly like two hardware knobs on one param.
// - AudioBand triggers are ADDITIVE-THEN-CLAMPED: computed fresh every
//   frame on top of whatever the target's value currently is (the absolute
//   store's value if any, else the scene's static baseline), so a bass
//   pulse rides on top of a knob without fighting it.
// - onSceneEnter() swaps the whole table: the store clears and CC-mapped
//   targets seed from wherever each physical knob is currently latched --
//   the same snap-to-current-position behavior as a hardware synth patch
//   change. Called on scene switch and show hot-reload alike.
class MappingResolver {
public:
    void onSceneEnter(const Scene& scene, const std::map<int, float>& ccValues);

    // Manual injection from the command FIFO (browser Live mode / editor
    // instant-feedback). A `cc` command is indistinguishable from the same
    // CC arriving over MIDI; a `param` command pins one target directly.
    void setManualCc(int ccNumber, float value01);
    void setManualParam(const std::string& layerId, const std::string& param, float value);

    LiveParams resolve(const Scene& scene, const ControlState& controlState);

private:
    using TargetKey = std::pair<std::string, std::string>;  // {layerId ("" = scene scope), param}

    void applyCcValue(const Scene& scene, int ccNumber, float value01);

    std::map<TargetKey, float> absoluteStore;
    std::map<int, float> prevCcValues;
    std::map<int, float> pendingManualCc;
};
