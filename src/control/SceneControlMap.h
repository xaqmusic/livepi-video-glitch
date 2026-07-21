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
    // Whether a settings file is wired at all (appliance yes, bare desktop dev
    // no) -- lets callers skip pushing live overrides that would otherwise stomp
    // config defaults with this map's own defaults.
    bool configured() const { return !path.empty(); }
    // Cheap mtime check; reloads the bindings only when the file changed.
    void pollForChanges();
    // Edge-detects the mapped controls against this frame and returns the scene
    // action to inject (Click/Hold) or None. Tracks previous values internally,
    // so call it exactly once per frame.
    ButtonEvent poll(const ControlState& state);

    // Global thermal-rescue toggle (also lives in settings.json, set from the
    // gear menu). Hot-reloaded with the rest of the file. Defaults on.
    bool thermalRescue() const { return thermalRescueEnabled; }
    // Whether a mid-scene thermal step-down is masked with the scene's
    // transition (else it resizes silently). Defaults off -- a random static/
    // tear mid-scene reads as a glitch; the scene just restarting is quieter.
    bool thermalTransition() const { return thermalTransitionEnabled; }

    // Live audio tuning from the gear menu, hot-reloaded with the rest of the
    // file: the overall-level one-pole smoothing coefficient (0..0.98) and the
    // adaptive-gain toggle. ofApp pushes these onto the control source.
    float audioSmoothing() const { return audioSmoothingValue; }
    bool audioAutoGain() const { return audioAutoGainEnabled; }

private:
    void load();
    bool risingEdge(const Trigger& t, const ControlState& state, float& prev) const;

    std::string path;
    std::time_t lastMtime = 0;
    bool everLoaded = false;
    Trigger advanceTrigger;
    Trigger backTrigger;
    bool thermalRescueEnabled = true;
    bool thermalTransitionEnabled = false;
    float audioSmoothingValue = 0.6f;
    bool audioAutoGainEnabled = true;
    float prevAdvance = 0.0f;
    float prevBack = 0.0f;
};
