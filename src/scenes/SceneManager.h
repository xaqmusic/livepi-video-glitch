#pragma once

#include <string>
#include <vector>

#include "Scene.h"
#include "control/ControlState.h"

// Owns the ordered list of scenes and which one is active. Advances on a
// Click ButtonEvent; a Hold jumps back to the first scene (useful for
// recovering mid-set without hunting through the list). Scenes come from
// ShowLoader (the active show file), not device config.
class SceneManager {
public:
    // Replaces the scene list. Falls back to a single built-in scene if
    // given nothing, so the renderer always has something to draw.
    void setScenes(std::vector<Scene> newScenes);

    // Same replacement, but keeps the *current* scene selected by stable id
    // when it still exists in the new list (its index may have changed).
    // Used on show hot-reload so an edit elsewhere in the setlist doesn't
    // yank the performer off the scene they're on.
    void retainSceneById(std::vector<Scene> newScenes);

    void update(const ControlState& controlState);

    // Applies a button event immediately -- the command FIFO's path into
    // scene control (browser next/back), equivalent to the same event
    // arriving via ControlState.
    void injectButtonEvent(ButtonEvent event);

    // Jump directly to a scene by id (Live mode's "back" button). No-op
    // with a warning if the id doesn't exist.
    void gotoSceneById(const std::string& id);

    const Scene& getCurrentScene() const;
    const std::string& getCurrentSceneId() const { return getCurrentScene().id; }
    size_t getCurrentIndex() const { return currentIndex; }
    size_t getSceneCount() const { return scenes.size(); }

private:
    void applyButtonEvent(ButtonEvent event);

    std::vector<Scene> scenes;
    size_t currentIndex = 0;
};
