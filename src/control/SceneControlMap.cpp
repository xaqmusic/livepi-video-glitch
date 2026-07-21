#include "control/SceneControlMap.h"

#include <sys/stat.h>

#include <algorithm>

#include "ofFileUtils.h"
#include "ofLog.h"

namespace {

SceneControlMap::Trigger parseTrigger(const ofJson& root, const std::string& key) {
    SceneControlMap::Trigger t;
    auto it = root.find(key);  // find() is safe on any json value (end() if not object)
    if (it == root.end() || it->is_null() || !it->is_object()) return t;
    const ofJson& node = *it;
    if (!node.contains("type") || !node.contains("number") || !node["number"].is_number_integer()) {
        return t;
    }
    const std::string type = node.value("type", "");
    if (type == "note") t.type = SceneControlMap::Trigger::Type::Note;
    else if (type == "cc") t.type = SceneControlMap::Trigger::Type::CC;
    t.number = node["number"].get<int>();
    if (!t.active()) t = SceneControlMap::Trigger{};  // unknown type / bad number -> unbound
    return t;
}

}  // namespace

void SceneControlMap::setup(const std::string& jsonPath) {
    path = jsonPath;
    lastMtime = 0;
    if (path.empty()) {
        ofLogNotice("SceneControlMap") << "no scene-control map configured (controls.scene_map unset)";
        return;
    }
    load();
}

void SceneControlMap::pollForChanges() {
    if (path.empty()) return;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) {
        // The file isn't there (not written yet, or deleted). If it vanished
        // after we'd loaded bindings, drop them so a removed file disables
        // switching rather than leaving a stale binding live.
        if (everLoaded && (advanceTrigger.active() || backTrigger.active())) {
            advanceTrigger = Trigger{};
            backTrigger = Trigger{};
            ofLogNotice("SceneControlMap") << "scene-control file gone -- bindings cleared";
        }
        lastMtime = 0;
        return;
    }
    if (st.st_mtime != lastMtime) load();
}

void SceneControlMap::load() {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) {
        lastMtime = 0;
        return;
    }
    lastMtime = st.st_mtime;
    everLoaded = true;
    const ofJson root = ofLoadJson(path);
    advanceTrigger = parseTrigger(root, "sceneAdvance");
    backTrigger = parseTrigger(root, "sceneBack");
    thermalRescueEnabled = root.is_object() ? root.value("thermalRescue", true) : true;
    thermalTransitionEnabled = root.is_object() ? root.value("thermalTransition", false) : false;
    audioSmoothingValue =
        root.is_object() ? std::clamp(root.value("audioSmoothing", 0.6f), 0.0f, 0.98f) : 0.6f;
    audioAutoGainEnabled = root.is_object() ? root.value("audioAutoGain", true) : true;
    ofLogNotice("SceneControlMap")
        << "loaded device settings from " << path
        << " (advance " << (advanceTrigger.active() ? "bound" : "none")
        << ", back " << (backTrigger.active() ? "bound" : "none")
        << ", thermalRescue " << (thermalRescueEnabled ? "on" : "off")
        << ", thermalTransition " << (thermalTransitionEnabled ? "on" : "off")
        << ", audioSmoothing " << audioSmoothingValue
        << ", audioAutoGain " << (audioAutoGainEnabled ? "on" : "off") << ")";
}

bool SceneControlMap::risingEdge(const Trigger& t, const ControlState& state, float& prev) const {
    if (!t.active()) {
        prev = 0.0f;
        return false;
    }
    const std::map<int, float>& m = (t.type == Trigger::Type::Note) ? state.noteValues : state.ccValues;
    auto it = m.find(t.number);
    const float cur = (it != m.end()) ? it->second : 0.0f;
    // A note press is any velocity > 0; a CC used as a button crosses the
    // half-way point, so a knob swept up fires once (not on every step) and a
    // momentary CC switch (0 <-> 127) fires on the press.
    const float threshold = (t.type == Trigger::Type::Note) ? 0.01f : 0.5f;
    const bool edge = prev <= threshold && cur > threshold;
    prev = cur;
    return edge;
}

ButtonEvent SceneControlMap::poll(const ControlState& state) {
    // Advance BOTH edge detectors every frame so neither prev goes stale; if
    // both fire the same frame, advance wins.
    const bool advanceEdge = risingEdge(advanceTrigger, state, prevAdvance);
    const bool backEdge = risingEdge(backTrigger, state, prevBack);
    if (advanceEdge) return ButtonEvent::Click;
    if (backEdge) return ButtonEvent::Hold;
    return ButtonEvent::None;
}
