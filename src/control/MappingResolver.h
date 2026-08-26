#pragma once

#include <map>
#include <string>
#include <utility>

#include "ControlState.h"
#include "scenes/LiveParams.h"
#include "scenes/Scene.h"
#include "util/ParamDomains.h"

// The per-frame step that turns the active scene's mapping table plus the
// live inputs (MIDI CCs, audio-band envelopes, browser commands) into the
// LiveParams view everything downstream reads. Semantics per
// docs/videosynth-backend.md:
//
// - ALL triggers are now ADDITIVE-THEN-CLAMPED (CC, Note and AudioBand
//   alike): every frame, each mapping's current level is multiplied by its
//   target's span (max-min) and SUMMED onto the target's baseline -- the
//   manual store's value if the operator has touched the slider, else the
//   scene's static baseline. The sum is clamped to the param's real domain
//   from effects_manifest.json (see ParamDomains).
//
//   CC used to be ABSOLUTE -- the resolved value WAS the target's value,
//   edge-detected and latched in a per-scene store. That was changed on
//   request: a knob should OFFSET from where the slider sits, not overwrite
//   it, which is what makes a negative amount read as "invert" (turn the
//   knob up and the value goes down from your setting) instead of just
//   sweeping backwards over a range you no longer control. A binding's
//   `amount` is carried in the target span itself, so an old binding written
//   as [spec.min, spec.max] still means amount 1.0 and needs no migration.
//
//   Consequence worth knowing: with the slider already at a param's floor a
//   negative amount has nowhere to go and reads as dead. That is inherent to
//   an additive model -- give it a baseline with headroom.
// - onSceneEnter() swaps the whole table: the manual store clears and the
//   latched input levels seed from wherever each physical knob currently
//   sits, so entering a scene picks up the hardware's real positions rather
//   than snapping them to zero. Called on scene switch and show hot-reload.
class MappingResolver {
public:
    void onSceneEnter(const Scene& scene, const std::map<int, float>& ccValues);

    // Manual injection from the command FIFO (browser Live mode / editor
    // instant-feedback). A `cc` command is indistinguishable from the same
    // CC arriving over MIDI; a `param` command pins one target directly.
    void setManualCc(int ccNumber, float value01);
    void setManualNote(int noteNumber, float value01);
    void setManualParam(const std::string& layerId, const std::string& param, float value);

    LiveParams resolve(const Scene& scene, const ControlState& controlState);

private:
    using TargetKey = std::pair<std::string, std::string>;  // {layerId ("" = scene scope), param}

    // Manual param pins only (slider drags via the `param` command). Trigger
    // contributions are recomputed every frame and never stored here, so a
    // knob can no longer overwrite the operator's own setting.
    std::map<TargetKey, float> absoluteStore;
    // Current level per input, latched across frames: a real CC/note edge or a
    // browser injection both write here, last writer wins -- so a phone slider
    // and a physical knob contend exactly like two knobs on one param.
    std::map<int, float> ccLatched;
    std::map<int, float> noteLatched;
    std::map<int, float> prevCcValues;
    std::map<int, float> prevNoteValues;
    ParamDomains domains;
};
