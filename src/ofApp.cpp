#include "ofApp.h"

#include "control/MockControlSource.h"
#include "fx/ChromaticAberrationPass.h"
#include "fx/HSyncTearPass.h"
#include "fx/StutterBufferPass.h"

void ofApp::setup() {
    ofSetVerticalSync(true);
    ofBackground(0);

    config.loadFromFile("config/app.json");
    config.mergeFromFile("config/app.local.json");

    int width = config.getInt("window.width", 1280);
    int height = config.getInt("window.height", 720);
    ofSetWindowShape(width, height);

    controlSource = createControlSource(config);
    controlSource->setup(config);

    sceneManager.setup(config);

    shaderChain.setup(width, height);
    shaderChain.addPass(std::make_unique<HSyncTearPass>());
    shaderChain.addPass(std::make_unique<ChromaticAberrationPass>());
    shaderChain.addPass(std::make_unique<StutterBufferPass>());

    loadCurrentScene();
}

void ofApp::update() {
    controlSource->update();
    sceneManager.update(controlSource->getState());

    if (sceneManager.getCurrentIndex() != lastLoadedSceneIndex) {
        loadCurrentScene();
    }

    clipPlayer.update();
}

void ofApp::loadCurrentScene() {
    const Scene& scene = sceneManager.getCurrentScene();
    clipPlayer.load(scene.clipPath);
    lastLoadedSceneIndex = sceneManager.getCurrentIndex();
}

void ofApp::draw() {
    if (clipPlayer.isLoaded()) {
        shaderChain.process(clipPlayer.getTexture(), controlSource->getState(), sceneManager.getCurrentScene());
        shaderChain.getOutputFbo().draw(0, 0, ofGetWidth(), ofGetHeight());
    }

    if (showDebugOverlay) {
        const ControlState& state = controlSource->getState();
        std::stringstream ss;
        ss << "scene: " << sceneManager.getCurrentScene().name << " (" << sceneManager.getCurrentIndex() + 1 << "/"
           << sceneManager.getSceneCount() << ")\n"
           << "bpm: " << state.bpmEstimate << (state.clockPresent ? "" : "  (free-running, no clock)") << "\n"
           << "beat: " << state.beatInBar << "  bar: " << state.barNumber << "\n"
           << "knobA: " << state.knobA << "  knobB: " << state.knobB << "\n"
           << "audioLevel: " << state.audioLevel << "\n"
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

    // Only the mock backend cares about keyboard input; the real Pisound
    // backend gets its button/knob events from MIDI/FIFO instead.
    if (auto* mock = dynamic_cast<MockControlSource*>(controlSource.get())) {
        mock->keyPressed(key);
    }
}
