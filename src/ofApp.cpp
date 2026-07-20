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
#include "util/NetInfo.h"

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
    // current size (the real display's native resolution in fullscreen mode).
    //
    // render.scale (default 1.0 = native) shrinks the INTERNAL render below the
    // display, trading sharpness for frame rate on heavy scenes -- the output
    // FBO is simply presented back up to the full window (see draw()). It
    // multiplies both dimensions, so aspect is preserved; a low-fps-pretty look
    // is a valid artistic choice, and a Pi 5 will just leave it at 1.0. Clamped
    // so a typo can't ask for a 1px or larger-than-native target.
    baseW = ofGetWidth();
    baseH = ofGetHeight();
    configRenderScale = std::clamp(config.getFloat("render.scale", 1.0f), 0.25f, 1.0f);
    int renderW = std::max(64, static_cast<int>(std::lround(baseW * configRenderScale)));
    int renderH = std::max(64, static_cast<int>(std::lround(baseH * configRenderScale)));
    sceneRenderer.setup(renderW, renderH);
    // Resolution is now per-scene (Scene::renderScale); render.scale stays as an
    // optional GLOBAL ceiling. The renderer sizes each scene to base * min(scene
    // scale, ceiling) as it enters, under that scene's transition.
    sceneRenderer.setBaseSize(baseW, baseH);
    sceneRenderer.setMaxScale(configRenderScale);
    if (configRenderScale < 0.999f) {
        ofLogNotice("ofApp") << "render.scale ceiling=" << configRenderScale
                             << " (per-scene renderScale caps under it), display " << baseW << "x" << baseH;
    }
    // Thermal rescue: cap the internal render scale when the SoC overheats (keeps
    // a heavy scene alive instead of throttling to black). Always set up so the
    // live toggle (settings.json, via sceneControlMap.thermalRescue) can enable
    // it at any time without a restart.
    thermal.setup();
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

    // Read the hostname / device code / AP name once for the setup overlay.
    connectionCard.gather();

    // The setup card defaults on (showConnectionCard = true) so a fresh box
    // greets its new owner with the AP/code/URL/QR. Once that owner has logged
    // into the web UI once, the backend drops a persistent marker on /data;
    // from then on the box is "claimed" and the card must NOT re-pop every
    // boot. ui.claimed_marker is that marker's path (empty on desktop dev, set
    // to $DATA_DIR/.claimed on the appliance) -- if it exists at startup, start
    // with the card already retired. Absolute path, so not relative to data/.
    // Re-showable anytime via the button hold, [c], or the gear-menu toggle.
    const std::string claimedMarker = config.getString("ui.claimed_marker", "");
    if (!claimedMarker.empty() && ofFile::doesFileExist(claimedMarker, false)) {
        showConnectionCard = false;
        cardDismissed = true;
    }

    // A MIDI note/CC bound to scene switching in the gear menu, if any. The
    // backend persists it into settings.json; we watch that file (absolute path
    // set on the appliance, empty => feature off).
    sceneControlMap.setup(config.getString("controls.scene_map", ""));

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
    auto commands = commandFifo.poll();
    for (const auto& cmd : commands) {
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
            case CommandFifo::Command::Type::Card:
                // Setup overlay: backend sends "card off" on login; a long
                // button press toggles. value: -1 toggle, 0 off, 1 on.
                showConnectionCard = cmd.value < -0.5f ? !showConnectionCard : cmd.value > 0.5f;
                cardDismissed = true;  // an explicit command wins; don't auto-repop
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

    // The setup overlay auto-hides the first time someone's clearly connected:
    // any web command, a played note, or the scene button. [c] brings it back.
    if (showConnectionCard && !cardDismissed) {
        bool interacted = !commands.empty() || frameState.lastButtonEvent != ButtonEvent::None;
        for (const auto& [note, vel] : frameState.noteValues) {
            if (vel > 0.01f) {
                interacted = true;
                break;
            }
        }
        if (interacted) {
            showConnectionCard = false;
            cardDismissed = true;
        }
    }

    // A learned MIDI/CC scene-switch binding (gear menu) acts here: hot-reload
    // the binding if it changed, then edge-detect it against this frame's notes/
    // CCs and inject the same Click/Hold the physical button would. frameState
    // already has FIFO-injected notes merged in, so a web-played note counts too.
    sceneControlMap.pollForChanges();
    ButtonEvent sceneButton = sceneControlMap.poll(frameState);
    if (sceneButton != ButtonEvent::None) {
        sceneManager.injectButtonEvent(sceneButton);
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

    // Refresh the overlay's connection status on a slow cadence -- IPs only
    // change on a network event, and getifaddrs every frame is wasteful.
    const float now = ofGetElapsedTimef();
    if (now - lastNetRefreshSecs > 2.0f) {
        netSummary = NetInfo::summary();
        lastNetRefreshSecs = now;
    }

    // Effective internal render scale = the tightest of this scene's own setting,
    // the optional global ceiling, and the thermal cap. Per-scene gives balance;
    // thermal is a GLOBAL OVERRIDE that only pulls a high scene down (never up).
    // A scene switch already applies its own scale under the entry transition
    // (SceneRenderer::loadScene); this catches thermal tier steps mid-scene and
    // any residual, comparing against the live render width so a resize deferred
    // during a transition just retries next tick. The governor self-throttles its
    // sysfs read, so calling update() every frame is cheap.
    const float thermalCap = sceneControlMap.thermalRescue() ? thermal.update(now) : 1.0f;
    const float sceneScale = std::clamp(sceneManager.getCurrentScene().renderScale, 0.25f, 1.0f);
    const float effective = std::min({sceneScale, configRenderScale, thermalCap});
    const int wantW = std::max(64, static_cast<int>(std::lround(baseW * effective)));
    const int wantH = std::max(64, static_cast<int>(std::lround(baseH * effective)));
    if (wantW != sceneRenderer.renderWidth()) {
        sceneRenderer.requestResize(wantW, wantH);
    }
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

    if (showConnectionCard) {
        connectionCard.draw(ofGetWidth(), ofGetHeight());
    }

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
           << "render: " << sceneRenderer.renderWidth() << "px wide"
           << "  scene scale " << std::clamp(sceneManager.getCurrentScene().renderScale, 0.25f, 1.0f)
           << (sceneControlMap.thermalRescue()
                   ? "  soc " + std::to_string(static_cast<int>(thermal.tempC())) + "C"
                   : "  thermal off")
           << (sceneControlMap.thermalRescue() && thermal.scale() < 0.999f ? "  [THERMAL-CAPPED]" : "")
           << "\n"
           << "net: " << netSummary << "   [web :8080]\n"
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
    if (key == 'c') {
        // Toggle the setup overlay; mark dismissed so it doesn't auto-pop again.
        showConnectionCard = !showConnectionCard;
        cardDismissed = true;
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
