#pragma once

#include <vector>

#include "Scene.h"
#include "control/ControlState.h"

class Config;

// Owns the ordered list of scenes and which one is active. Advances on a
// Click ButtonEvent; a Hold jumps back to the first scene (useful for
// recovering mid-set without hunting through the list).
class SceneManager {
public:
    void setup(const Config& config);
    void update(const ControlState& controlState);

    const Scene& getCurrentScene() const;
    size_t getCurrentIndex() const { return currentIndex; }
    size_t getSceneCount() const { return scenes.size(); }

private:
    std::vector<Scene> scenes;
    size_t currentIndex = 0;
};
