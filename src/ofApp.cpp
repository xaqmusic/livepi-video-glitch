#include "ofApp.h"

#include "control/MidiControlSource.h"
#include "control/MockControlSource.h"
#include "fx/ChromaticAberrationPass.h"
#include "fx/HSyncTearPass.h"
#include "fx/StutterBufferPass.h"

void ofApp::setup() {
    ofSetVerticalSync(true);
    ofBackground(0);

    // Cheap to log, expensive to debug blind -- see "GL / GLES portability"
    // in docs/architecture.md for why the actual negotiated context/version
    // is worth confirming on every new piece of hardware this runs on.
    ofLogNotice("ofApp") << "GL renderer: " << glGetString(GL_RENDERER) << ", version: " << glGetString(GL_VERSION);

    config.loadFromFile("config/app.json");
    config.mergeFromFile("config/app.local.json");

    controlSource = createControlSource(config);
    controlSource->setup(config);

    // Scenes live in the active show (bin/data/shows/), not app.json --
    // app.json is device config only. A failed load falls through to
    // SceneManager's built-in fallback scene.
    showLoader.load();
    sceneManager.setScenes(showLoader.getScenes());

    // The window itself (size, fullscreen-vs-windowed) is already set up in
    // main.cpp before this runs -- ofGetWidth/Height reflect its actual
    // current size (the real display's native resolution in fullscreen
    // mode), so the render pipeline always matches whatever's really on
    // screen instead of a second, possibly-mismatched config read.
    sceneRenderer.setup(ofGetWidth(), ofGetHeight());
    sceneRenderer.addPostPass(std::make_unique<HSyncTearPass>());
    sceneRenderer.addPostPass(std::make_unique<ChromaticAberrationPass>());
    sceneRenderer.addPostPass(std::make_unique<StutterBufferPass>());

    loadCurrentScene();
}

void ofApp::update() {
    controlSource->update();
    sceneManager.update(controlSource->getState());

    if (sceneManager.getCurrentIndex() != lastLoadedSceneIndex) {
        loadCurrentScene();
    }

    liveParams = mappingResolver.resolve(sceneManager.getCurrentScene(), controlSource->getState());
    sceneRenderer.update();
}

void ofApp::loadCurrentScene() {
    const Scene& scene = sceneManager.getCurrentScene();
    sceneRenderer.loadScene(scene);
    // Swap the mapping table with the scene: the store clears and CC-mapped
    // targets snap to wherever each knob currently sits, hardware-synth
    // patch-change style.
    mappingResolver.onSceneEnter(scene, controlSource->getState().ccValues);
    lastLoadedSceneIndex = sceneManager.getCurrentIndex();
}

void ofApp::draw() {
    sceneRenderer.render(controlSource->getState(), liveParams);
    ofSetColor(255);
    sceneRenderer.getOutputFbo().draw(0, 0, ofGetWidth(), ofGetHeight());

    if (showDebugOverlay) {
        const ControlState& state = controlSource->getState();
        std::stringstream ss;
        ss << "scene: " << sceneManager.getCurrentScene().name << " (" << sceneManager.getCurrentIndex() + 1 << "/"
           << sceneManager.getSceneCount() << ")  show: " << showLoader.getActiveShowName() << "\n"
           << "bpm: " << state.bpmEstimate << (state.clockPresent ? "" : "  (free-running, no clock)") << "\n"
           << "beat: " << state.beatInBar << "  bar: " << state.barNumber << "\n"
           << "mappings: " << sceneManager.getCurrentScene().mappings.size()
           << "  lastCC: " << state.lastCcEvent.number << "=" << state.lastCcEvent.value01 << "\n"
           << "hsync: " << liveParams.getParam("hsync.intensity", 0.5f)
           << "  chromatic: " << liveParams.getParam("chromatic.intensity", 0.5f)
           << "  stutter: " << liveParams.getParam("stutter.enabled", 1.0f) << "\n"
           << "audioLevel: " << state.audioLevel << "\n"
           << "bands  low: " << state.lowBand << "  mid: " << state.midBand << "  high: " << state.highBand << "\n"
           << "window: " << ofGetWidth() << "x" << ofGetHeight() << "  layers: " << sceneRenderer.getLayerCount()
           << "\n"
           << sceneRenderer.describeLayers() << "\n"
           << "app fps: " << ofGetFrameRate() << "  (t=" << ofGetElapsedTimef() << ")\n"
           << "[d] toggle this overlay";
        ofSetColor(255);
        ofDrawBitmapStringHighlight(ss.str(), 20, 20);
    }
}

void ofApp::keyPressed(int key) {
    if (key == 'd') {
        showDebugOverlay = !showDebugOverlay;
        return;
    }

    // Mock uses the keyboard for everything; MidiControlSource only wants it
    // as a stand-in scene button (real knobs/clock come from MIDI); the real
    // Pisound backend gets its button from the FIFO instead and ignores this.
    if (auto* mock = dynamic_cast<MockControlSource*>(controlSource.get())) {
        mock->keyPressed(key);
    } else if (auto* midi = dynamic_cast<MidiControlSource*>(controlSource.get())) {
        midi->keyPressed(key);
    }
}
