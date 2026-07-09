#pragma once

#include <memory>

#include "control/ControlSource.h"
#include "control/MappingResolver.h"
#include "ofMain.h"
#include "render/SceneRenderer.h"
#include "scenes/LiveParams.h"
#include "scenes/SceneManager.h"
#include "scenes/ShowLoader.h"
#include "util/Config.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    void loadCurrentScene();

    Config config;
    std::unique_ptr<ControlSource> controlSource;
    ShowLoader showLoader;
    SceneManager sceneManager;
    SceneRenderer sceneRenderer;
    MappingResolver mappingResolver;
    LiveParams liveParams;

    size_t lastLoadedSceneIndex = static_cast<size_t>(-1);
    bool showDebugOverlay = true;
};
