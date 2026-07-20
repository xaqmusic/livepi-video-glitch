#pragma once

#include <memory>

#include "control/CommandFifo.h"
#include "control/ControlSource.h"
#include "control/MappingResolver.h"
#include "control/SceneControlMap.h"
#include "ofMain.h"
#include "render/SceneRenderer.h"
#include "scenes/LiveParams.h"
#include "scenes/SceneManager.h"
#include "scenes/ShowLoader.h"
#include "util/Config.h"
#include "util/ConnectionCard.h"
#include "util/ThermalGovernor.h"
#include "util/TelemetryWriter.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void exit() override;

private:
    void loadCurrentScene();

    Config config;
    std::unique_ptr<ControlSource> controlSource;

    // The frame's working control state: the source's state plus FIFO-
    // injected notes overlaid, so browser/test note commands reach note-
    // triggered generators exactly like keys played on the hardware.
    ControlState frameState;
    std::map<int, float> fifoNotes;
    ShowLoader showLoader;
    SceneManager sceneManager;
    SceneRenderer sceneRenderer;
    MappingResolver mappingResolver;
    // A MIDI note/CC learned in the gear menu that switches scenes, watched from
    // the backend's settings.json (empty config path => disabled).
    SceneControlMap sceneControlMap;
    LiveParams liveParams;
    TelemetryWriter telemetryWriter;
    CommandFifo commandFifo;

    size_t lastLoadedSceneIndex = static_cast<size_t>(-1);
    // Starts hidden -- toggle with [d] locally or the Live-mode debug button
    // (a "debug" command on the FIFO).
    bool showDebugOverlay = false;

    // On-screen setup overlay (Wi-Fi name, device code, URL). Shows on boot so
    // a fresh box tells you how to connect, then auto-hides the first time
    // anyone interacts (a note, the button, or any web command -- they're
    // clearly connected by then). [c] toggles it back.
    ConnectionCard connectionCard;
    bool showConnectionCard = true;
    bool cardDismissed = false;

    // Cached network reachability for the debug overlay, refreshed on a slow
    // cadence (getifaddrs is cheap but pointless to run every frame).
    std::string netSummary;
    float lastNetRefreshSecs = -1000.0f;

    // Thermal rescue: drop the internal render resolution when the Pi overheats
    // so a heavy scene degrades gracefully instead of throttling to black. The
    // effective scale is the render.scale config baseline times the governor's
    // tier; the display is always the full window (the output FBO upscales).
    ThermalGovernor thermal;
    bool thermalRescue = true;
    int baseW = 0;                    // display size; render target = base * scale
    int baseH = 0;
    float configRenderScale = 1.0f;   // render.scale config baseline
};
