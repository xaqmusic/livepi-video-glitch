#include "SceneManager.h"

#include "ofLog.h"
#include "util/Config.h"

void SceneManager::setup(const Config& config) {
    scenes = config.getScenes();
    if (scenes.empty()) {
        ofLogWarning("SceneManager") << "No scenes configured, using a single fallback scene.";
        Scene fallback;
        fallback.name = "fallback";
        fallback.clipPath = "clips/samples/sample_crt_loop_01.mp4";
        scenes.push_back(fallback);
    }
    currentIndex = 0;
}

void SceneManager::update(const ControlState& controlState) {
    if (controlState.lastButtonEvent == ButtonEvent::Click) {
        currentIndex = (currentIndex + 1) % scenes.size();
    } else if (controlState.lastButtonEvent == ButtonEvent::Hold) {
        currentIndex = 0;
    }
}

const Scene& SceneManager::getCurrentScene() const {
    return scenes[currentIndex];
}
