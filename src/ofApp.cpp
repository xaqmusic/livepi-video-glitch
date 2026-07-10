#include "ofApp.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <iomanip>

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
    // Ping-pong reverse is a baked boomerang (forward+reverse in one forward-
    // looping file), prepped per clip+trim by the backend -- the Pi's v4l2
    // decoder stalls on rate -1, so nothing here ever plays backwards.
    // Stutter is a per-layer effect now (SceneRenderer adds it to each
    // clip layer's chain) -- the post chain is the frame-wide CRT decay.
    // Static is the raw signal snow, so it lands FIRST: the grade, tear,
    // aberration and tube all then process the noisy signal.
    sceneRenderer.addPostPass(std::make_unique<StaticPass>());
    // The global grade comes next: correct the composite, then decay it.
    sceneRenderer.addPostPass(std::make_unique<ColorAdjustPass>());
    sceneRenderer.addPostPass(std::make_unique<HSyncTearPass>());
    sceneRenderer.addPostPass(std::make_unique<ChromaticAberrationPass>());
    // Scan lines then the tube: barrel curves the lines with the glass, so
    // scan lines come just before it (both are the physical monitor).
    sceneRenderer.addPostPass(std::make_unique<ScanlinesPass>());
    sceneRenderer.addPostPass(std::make_unique<BarrelPass>());
    // Transition-only passes (idle-skipped like everything else): a
    // scene-scoped fracture for the "shatter" style, and the dip-to-black
    // fade dead last so it darkens the finished frame.
    sceneRenderer.addPostPass(std::make_unique<FracturePass>());
    sceneRenderer.addPostPass(std::make_unique<FadePass>());

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
            case CommandFifo::Command::Type::Debug:
                // Same toggle as the [d] key, reachable from Live mode where
                // there's no keyboard.
                showDebugOverlay = !showDebugOverlay;
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
    sceneRenderer.update(liveParams);

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
        // 5-segment ASCII meter for a normalized 0..1 band level.
        auto meter = [](float v) {
            int filled = std::clamp(static_cast<int>(std::lround(v * 5.0f)), 0, 5);
            std::string m = "[";
            for (int i = 0; i < 5; i++) m += (i < filled ? '#' : '-');
            return m + "]";
        };
        std::stringstream ss;
        // Fixed two-decimal floats everywhere so line lengths don't shift
        // as values move.
        ss << std::fixed << std::setprecision(2);
        ss << "scene: " << sceneManager.getCurrentScene().name << " (" << sceneManager.getCurrentIndex() + 1 << "/"
           << sceneManager.getSceneCount() << ")  show: " << showLoader.getActiveShowName() << "\n"
           << "bpm: " << state.bpmEstimate << (state.clockPresent ? "  (midi clock)" : "  (free-running)") << "\n"
           << "mappings: " << sceneManager.getCurrentScene().mappings.size() << "  last: "
           << (state.lastControlEvent.kind == LastControlEvent::Kind::Note ? "note " : "cc ")
           << state.lastControlEvent.number << "=" << state.lastControlEvent.value01 << "\n"
           << "audio  low " << meter(state.lowBand) << "  mid " << meter(state.midBand) << "  high "
           << meter(state.highBand) << "\n"
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
