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

    sceneManager.setup(config);

    shaderChain.addPass(std::make_unique<HSyncTearPass>());
    shaderChain.addPass(std::make_unique<ChromaticAberrationPass>());
    shaderChain.addPass(std::make_unique<StutterBufferPass>());
    // The window itself (size, fullscreen-vs-windowed) is already set up in
    // main.cpp before this runs -- ofGetWidth/Height reflect its actual
    // current size (the real display's native resolution in fullscreen
    // mode), so the effect chain always matches whatever's really on
    // screen instead of a second, possibly-mismatched config read.
    shaderChain.setup(ofGetWidth(), ofGetHeight());

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
        shaderChain.process(clipPlayer.getDrawable(), controlSource->getState(), sceneManager.getCurrentScene());
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
           << "bands  low: " << state.lowBand << "  mid: " << state.midBand << "  high: " << state.highBand << "\n"
           << "window: " << ofGetWidth() << "x" << ofGetHeight() << "  fbo: " << shaderChain.getOutputFbo().getWidth()
           << "x" << shaderChain.getOutputFbo().getHeight() << "  clip: " << clipPlayer.getTexture().getWidth() << "x"
           << clipPlayer.getTexture().getHeight() << " " << clipPlayer.getPixelFormatName() << "\n"
           << "app fps: " << ofGetFrameRate() << "\n"
           << "clip pos: " << (clipPlayer.getPosition() * clipPlayer.getDuration()) << "s / "
           << clipPlayer.getDuration() << "s  (t=" << ofGetElapsedTimef() << ")\n"
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
