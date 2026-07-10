#include "ofApp.h"

#include <atomic>
#include <csignal>

#include "control/MidiControlSource.h"
#include "control/MockControlSource.h"
#include "fx/ChromaticAberrationPass.h"
#include "fx/FilterPasses.h"
#include "fx/HSyncTearPass.h"

namespace {
// systemd stops the kiosk with SIGTERM (to the whole cgroup). Dying
// abruptly leaks VideoCore decoder components firmware-side -- enough
// restarts and v4l2h264dec starts failing with "Failed to allocate
// required memory" until a reboot (observed on real hardware). Convert
// the signal into a clean oF exit so ClipPlayer/GStreamer teardown runs.
std::atomic<bool> quitRequested{false};
}  // namespace

void ofApp::setup() {
    ofSetVerticalSync(true);
    ofBackground(0);

    std::signal(SIGTERM, [](int) { quitRequested = true; });
    std::signal(SIGINT, [](int) { quitRequested = true; });

    // Cheap to log, expensive to debug blind -- see "GL / GLES portability"
    // in docs/architecture.md for why the actual negotiated context/version
    // is worth confirming on every new piece of hardware this runs on.
    ofLogNotice("ofApp") << "GL renderer: " << glGetString(GL_RENDERER) << ", version: " << glGetString(GL_VERSION);

    config.loadFromFile("config/app.json");
    config.mergeFromFile("config/app.local.json");

    controlSource = createControlSource(config);
    controlSource->setup(config);

    telemetryWriter.setup(config.getString("ipc.status_path", "/tmp/livepi/status.json"));
    commandFifo.setup(config.getString("ipc.command_fifo", "/tmp/livepi/command.fifo"));

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
    // Stutter is a per-layer effect now (SceneRenderer adds it to each
    // clip layer's chain) -- the post chain is the frame-wide CRT decay.
    sceneRenderer.addPostPass(std::make_unique<HSyncTearPass>());
    sceneRenderer.addPostPass(std::make_unique<ChromaticAberrationPass>());
    // Barrel is the tube itself, so it comes after every signal effect.
    sceneRenderer.addPostPass(std::make_unique<BarrelPass>());

    loadCurrentScene();
}

void ofApp::update() {
    if (quitRequested) {
        ofExit();
        return;
    }

    controlSource->update();

    // Browser commands (Live mode next/back, editor instant-feedback
    // nudges) -- applied before the scene-index check below so a
    // click/goto loads its scene this same frame.
    for (const auto& cmd : commandFifo.poll()) {
        switch (cmd.type) {
            case CommandFifo::Command::Type::Click:
                sceneManager.injectButtonEvent(ButtonEvent::Click);
                break;
            case CommandFifo::Command::Type::Hold:
                sceneManager.injectButtonEvent(ButtonEvent::Hold);
                break;
            case CommandFifo::Command::Type::Goto:
                sceneManager.gotoSceneById(cmd.sceneId);
                break;
            case CommandFifo::Command::Type::Cc:
                mappingResolver.setManualCc(cmd.ccNumber, cmd.value);
                break;
            case CommandFifo::Command::Type::Note:
                mappingResolver.setManualNote(cmd.ccNumber, cmd.value);
                // Also overlaid onto frameState.noteValues below, so note-
                // triggered generators hear FIFO notes like real keys.
                fifoNotes[cmd.ccNumber] = cmd.value;
                break;
            case CommandFifo::Command::Type::Param:
                // sceneId guards against a stale nudge racing a scene switch.
                if (cmd.sceneId == sceneManager.getCurrentSceneId()) {
                    mappingResolver.setManualParam(cmd.layerId, cmd.param, cmd.value);
                }
                break;
        }
    }

    frameState = controlSource->getState();
    for (auto it = fifoNotes.begin(); it != fifoNotes.end();) {
        frameState.noteValues[it->first] = it->second;
        // A release only needs delivering once (consumers edge-detect);
        // dropping it immediately keeps the overlay from masking the same
        // note played later on real hardware.
        if (it->second <= 0.0f) {
            it = fifoNotes.erase(it);
        } else {
            ++it;
        }
    }

    sceneManager.update(frameState);

    if (sceneManager.getCurrentIndex() != lastLoadedSceneIndex) {
        loadCurrentScene();
    }

    // Show hot-reload: the backend (or a hand edit) atomically replaced a
    // show file. Stay on the current scene by stable id; rebuild the layer
    // runtimes ONLY if the scene's layer structure actually changed --
    // param/mapping-only edits must never restart running clips (the
    // seam-aware reload rule the editor's save loop depends on).
    if (showLoader.pollForChanges()) {
        sceneManager.retainSceneById(showLoader.getScenes());
        const Scene& scene = sceneManager.getCurrentScene();
        if (!sceneRenderer.matchesRuntimes(scene)) {
            sceneRenderer.loadScene(scene);
        }
        mappingResolver.onSceneEnter(scene, frameState.ccValues);
        lastLoadedSceneIndex = sceneManager.getCurrentIndex();
    }

    liveParams = mappingResolver.resolve(sceneManager.getCurrentScene(), frameState);
    sceneRenderer.update();

    telemetryWriter.update(frameState, sceneManager.getCurrentSceneId(),
                           sceneManager.getCurrentScene().name);
}

void ofApp::exit() {
    // Destroy decoder sessions deliberately (pause -> close in ClipPlayer's
    // teardown) before the process goes away.
    sceneRenderer.loadScene(Scene{});
    controlSource->shutdown();
}

void ofApp::loadCurrentScene() {
    const Scene& scene = sceneManager.getCurrentScene();
    sceneRenderer.loadScene(scene);
    // Swap the mapping table with the scene: the store clears and CC-mapped
    // targets snap to wherever each knob currently sits, hardware-synth
    // patch-change style.
    mappingResolver.onSceneEnter(scene, frameState.ccValues);
    lastLoadedSceneIndex = sceneManager.getCurrentIndex();
}

void ofApp::draw() {
    sceneRenderer.render(frameState, liveParams);
    ofSetColor(255);
    sceneRenderer.getOutputFbo().draw(0, 0, ofGetWidth(), ofGetHeight());

    if (showDebugOverlay) {
        const ControlState& state = frameState;
        std::stringstream ss;
        ss << "scene: " << sceneManager.getCurrentScene().name << " (" << sceneManager.getCurrentIndex() + 1 << "/"
           << sceneManager.getSceneCount() << ")  show: " << showLoader.getActiveShowName() << "\n"
           << "bpm: " << state.bpmEstimate << (state.clockPresent ? "" : "  (free-running, no clock)") << "\n"
           << "beat: " << state.beatInBar << "  bar: " << state.barNumber << "\n"
           << "mappings: " << sceneManager.getCurrentScene().mappings.size() << "  last: "
           << (state.lastControlEvent.kind == LastControlEvent::Kind::Note ? "note " : "cc ")
           << state.lastControlEvent.number << "=" << state.lastControlEvent.value01 << "\n"
           << "hsync: " << liveParams.getParam("hsync.intensity", 0.5f)
           << "  chromatic: " << liveParams.getParam("chromatic.intensity", 0.5f) << "\n"
           << "audioLevel: " << state.audioLevel << "\n"
           << "bands  low: " << state.lowBand << "  mid: " << state.midBand << "  high: " << state.highBand << "\n"
           << "band peaks (raw)  low: " << state.lowPeakRaw << "  mid: " << state.midPeakRaw
           << "  high: " << state.highPeakRaw << "\n"
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
