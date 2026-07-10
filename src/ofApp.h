#pragma once

#include <memory>

#include "control/CommandFifo.h"
#include "control/ControlSource.h"
#include "control/MappingResolver.h"
#include "ofMain.h"
#include "render/SceneRenderer.h"
#include "scenes/LiveParams.h"
#include "scenes/SceneManager.h"
#include "scenes/ShowLoader.h"
#include "util/Config.h"
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
    LiveParams liveParams;
    TelemetryWriter telemetryWriter;
    CommandFifo commandFifo;

    size_t lastLoadedSceneIndex = static_cast<size_t>(-1);
    bool showDebugOverlay = true;
};
