#pragma once

#include <ctime>
#include <string>

#include "control/ControlState.h"
#include "ofJson.h"

// A device-global "this control switches scenes" binding, learned in the web
// UI's gear menu and persisted by the backend into settings.json on DATA_DIR.
// The renderer watches that file (mtime poll, like ShowLoader) so a binding
// learned live takes effect without a restart, and edge-detects the mapped
// note/CC each frame: the advance control fires a Click (next scene), the
// optional back control fires a Hold (jump to the first scene) -- the same two
// actions the physical button and the command FIFO already drive.
//
// An empty path disables the feature (desktop dev that hasn't opted in); the
// appliance points control.scene_map at $DATA_DIR/settings.json in app.local.json.
class SceneControlMap {
public:
    struct Trigger {
        enum class Type { None, CC, Note };
        Type type = Type::None;
        int number = -1;
        bool active() const { return type != Type::None && number >= 0; }
    };

    void setup(const std::string& jsonPath);
    // Cheap mtime check; reloads the bindings only when the file changed.
    void pollForChanges();
    // Edge-detects the mapped controls against this frame and returns the scene
    // action to inject (Click/Hold) or None. Tracks previous values internally,
    // so call it exactly once per frame.
    ButtonEvent poll(const ControlState& state);

private:
    void load();
    bool risingEdge(const Trigger& t, const ControlState& state, float& prev) const;

    std::string path;
    std::time_t lastMtime = 0;
    bool everLoaded = false;
    Trigger advanceTrigger;
    Trigger backTrigger;
    float prevAdvance = 0.0f;
    float prevBack = 0.0f;
};
