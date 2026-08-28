// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
#include "SceneManager.h"

#include "ofLog.h"
#include "ofUtils.h"  // ofGetElapsedTimef

namespace {

Scene makeFallbackScene() {
    Scene fallback;
    fallback.id = "scene-fallback";
    fallback.name = "fallback";
    fallback.clipPath = "clips/samples/sample_crt_loop_01.mp4";
    return fallback;
}

}  // namespace

void SceneManager::setScenes(std::vector<Scene> newScenes) {
    scenes = std::move(newScenes);
    if (scenes.empty()) {
        ofLogWarning("SceneManager") << "No scenes configured, using a single fallback scene.";
        scenes.push_back(makeFallbackScene());
    }
    currentIndex = 0;
}

void SceneManager::retainSceneById(std::vector<Scene> newScenes) {
    std::string previousId = getCurrentScene().id;
    size_t previousIndex = currentIndex;

    scenes = std::move(newScenes);
    if (scenes.empty()) {
        ofLogWarning("SceneManager") << "Reloaded show has no scenes, using a single fallback scene.";
        scenes.push_back(makeFallbackScene());
        currentIndex = 0;
        return;
    }

    for (size_t i = 0; i < scenes.size(); i++) {
        if (!previousId.empty() && scenes[i].id == previousId) {
            currentIndex = i;
            return;
        }
    }
    // Current scene was deleted: clamp to the nearest still-valid index
    // rather than jumping to the top of the set.
    currentIndex = std::min(previousIndex, scenes.size() - 1);
    ofLogNotice("SceneManager") << "Current scene \"" << previousId << "\" gone after reload -> scene "
                                << currentIndex << " (" << scenes[currentIndex].name << ")";
}

void SceneManager::applySceneSelect(const ControlState& controlState) {
    // The first frame only establishes a baseline. Without this, whatever a pad
    // or sequencer happened to be holding at startup would immediately yank the
    // box off the show's first scene.
    if (!sceneSelectPrimed) {
        prevNoteValues = controlState.noteValues;
        prevProgramChangeCount = controlState.programChangeCount;
        sceneSelectPrimed = true;
        return;
    }

    const bool programFired = controlState.programChangeCount != prevProgramChangeCount;
    prevProgramChangeCount = controlState.programChangeCount;

    for (const auto& scene : scenes) {
        if (!scene.select.active()) continue;

        bool fired = false;
        if (scene.select.type == SceneSelectType::Program) {
            fired = programFired && controlState.lastProgramChange == scene.select.number;
        } else if (scene.select.type == SceneSelectType::Note) {
            // Rising edge only: pressed now, not pressed last frame. A release
            // (velocity 0) must not select anything, or every note would fire
            // its scene twice.
            auto now = controlState.noteValues.find(scene.select.number);
            if (now != controlState.noteValues.end() && now->second > 0.0f) {
                auto before = prevNoteValues.find(scene.select.number);
                fired = before == prevNoteValues.end() || before->second <= 0.0f;
            }
        }

        if (fired && scene.id != getCurrentSceneId()) {
            ofLogNotice("SceneManager") << "MIDI select -> scene \"" << scene.name << "\"";
            gotoSceneById(scene.id);
            break;   // first match wins; a duplicate binding must not chain jumps
        }
    }

    prevNoteValues = controlState.noteValues;
}

void SceneManager::update(const ControlState& controlState) {
    if (controlState.lastButtonEvent != ButtonEvent::None) {
        applyButtonEvent(controlState.lastButtonEvent);
    }
    // After the button event, so an explicit scene selection wins over a
    // simultaneous next/previous nudge rather than being immediately overridden.
    applySceneSelect(controlState);

    const float now = ofGetElapsedTimef();
    // Restart the dwell clock whenever the active scene changes -- from the
    // button/MIDI event above, a goto-by-id, a show hot-reload, or the
    // auto-advance below. Detecting the index change in ONE place means no
    // mutation site has to remember to reset the timer.
    if (!dwellTracking || currentIndex != dwellTrackedIndex) {
        dwellTracking = true;
        dwellTrackedIndex = currentIndex;
        sceneEnteredAt = now;
    }

    // Timed auto-advance: when the active scene opts in, fire the same Click
    // the button/MIDI use once its dwell elapses, then restart the clock.
    // Needs >1 scene (Click wraps, so a lone scene would loop its transition).
    const Scene& current = scenes[currentIndex];
    if (current.autoAdvance && current.autoAdvanceSeconds > 0.0f && scenes.size() > 1
        && (now - sceneEnteredAt) >= current.autoAdvanceSeconds) {
        applyButtonEvent(ButtonEvent::Click);
        dwellTrackedIndex = currentIndex;
        sceneEnteredAt = now;
    }
}

void SceneManager::injectButtonEvent(ButtonEvent event) {
    applyButtonEvent(event);
}

void SceneManager::applyButtonEvent(ButtonEvent event) {
    if (event == ButtonEvent::Click) {
        currentIndex = (currentIndex + 1) % scenes.size();
        ofLogNotice("SceneManager") << "Click -> scene " << currentIndex << " (" << scenes[currentIndex].name << ")";
    } else if (event == ButtonEvent::Hold) {
        currentIndex = 0;
        ofLogNotice("SceneManager") << "Hold -> scene " << currentIndex << " (" << scenes[currentIndex].name << ")";
    }
}

const Scene& SceneManager::getCurrentScene() const {
    return scenes[currentIndex];
}

void SceneManager::gotoSceneById(const std::string& id) {
    for (size_t i = 0; i < scenes.size(); i++) {
        if (scenes[i].id == id) {
            currentIndex = i;
            ofLogNotice("SceneManager") << "Goto -> scene " << currentIndex << " (" << scenes[currentIndex].name
                                        << ")";
            return;
        }
    }
    ofLogWarning("SceneManager") << "gotoSceneById: no scene with id \"" << id << "\"";
}
