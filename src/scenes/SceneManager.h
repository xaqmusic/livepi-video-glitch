// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
#pragma once

#include <map>
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
    // Direct MIDI selection: match this frame's note presses / Program Change
    // against each scene's own SceneSelect and jump if one fires. Edge-detected
    // against the previous frame so a HELD pad doesn't re-enter the scene every
    // frame (which would restart its clips continuously).
    void applySceneSelect(const ControlState& controlState);

    std::vector<Scene> scenes;
    size_t currentIndex = 0;
    std::map<int, float> prevNoteValues;   // for note-press edge detection
    uint32_t prevProgramChangeCount = 0;   // for Program Change edge detection
    bool sceneSelectPrimed = false;        // ignore the very first frame

    // Timed auto-advance bookkeeping. sceneEnteredAt is the ofGetElapsedTimef()
    // stamp when the current scene became active; dwellTracking/dwellTrackedIndex
    // let update() notice ANY scene change (button/MIDI/goto/reload/timer) in one
    // place and restart the dwell clock, so no mutation site must remember to.
    size_t dwellTrackedIndex = 0;
    bool dwellTracking = false;
    float sceneEnteredAt = 0.0f;
};
