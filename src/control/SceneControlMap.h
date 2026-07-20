#pragma once

#include <ctime>
#include <string>

#include "control/ControlState.h"
#include "ofJson.h"

// The renderer's view of the device-global settings the backend persists into
// settings.json on DATA_DIR (set from the web UI's gear menu). It watches that
// file (mtime poll, like ShowLoader) so a change takes effect without a restart.
// Two things the renderer honors from it:
//   * scene-switch bindings -- a learned note/CC, edge-detected each frame: the
//     advance control fires a Click (next scene), the optional back control a
//     Hold (first scene), the same actions the button and command FIFO drive.
//   * thermalRescue -- the global toggle for the SoC-overheat resolution cap.
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

    // Global thermal-rescue toggle (also lives in settings.json, set from the
    // gear menu). Hot-reloaded with the rest of the file. Defaults on.
    bool thermalRescue() const { return thermalRescueEnabled; }

private:
    void load();
    bool risingEdge(const Trigger& t, const ControlState& state, float& prev) const;

    std::string path;
    std::time_t lastMtime = 0;
    bool everLoaded = false;
    Trigger advanceTrigger;
    Trigger backTrigger;
    bool thermalRescueEnabled = true;
    float prevAdvance = 0.0f;
    float prevBack = 0.0f;
};
