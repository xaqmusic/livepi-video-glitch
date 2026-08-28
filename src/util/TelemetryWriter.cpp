// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
#include "TelemetryWriter.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "ofAppRunner.h"
#include "ofFileUtils.h"
#include "ofJson.h"
#include "ofLog.h"
#include "ofUtils.h"
#include "util/Platform.h"

namespace {

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

TelemetryWriter::~TelemetryWriter() {
    watchdogRunning = false;
    if (watchdog.joinable()) watchdog.join();
}

void TelemetryWriter::setup(const std::string& statusPath) {
    path = statusPath;
    tmpPath = statusPath + ".tmp";
    ofDirectory::createDirectory(ofFilePath::getEnclosingDirectory(path, false), false, true);
    // The watchdog arms on the FIRST completed frame (see update()), not
    // here: setup() still has the scene's clip loads ahead of it, and a
    // cold boot legitimately spends 10s+ there -- arming early aborted the
    // app mid-startup on the first real power cycle.
}

void TelemetryWriter::update(const ControlState& state, const std::string& sceneId, const std::string& sceneName) {
    lastHeartbeatMs = nowMs();

    if (!watchdogRunning) {
        watchdogRunning = true;
        watchdog = std::thread([this] {
            while (watchdogRunning) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                long long silentMs = nowMs() - lastHeartbeatMs.load();
                // A known long op (a clip preroll) legitimately blocks the main
                // thread; hold off until its grace window expires so a slow cold
                // load isn't mistaken for a wedge. A genuinely hung op still
                // trips once the window passes.
                bool inLongOp = nowMs() < longOpUntilMs.load();
                if (watchdogRunning && !inLongOp && silentMs > static_cast<long long>(kWatchdogSecs * 1000)) {
                    // Frozen main thread (the rare v3d wedge) -- die loudly
                    // so systemd restarts the show instead of freezing until
                    // a human notices.
                    ofLogFatalError("TelemetryWriter")
                        << "No frame completed in " << (silentMs / 1000) << "s -- watchdog abort.";
                    std::abort();
                }
            }
        });
    }

    double now = ofGetElapsedTimef();
    if (now - lastWriteSecs < kWriteIntervalSecs) return;
    lastWriteSecs = now;
    writeStatus(state, sceneId, sceneName);
}

void TelemetryWriter::beginLongOp(double graceSecs) {
    longOpUntilMs = nowMs() + static_cast<long long>(graceSecs * 1000);
}

void TelemetryWriter::endLongOp() {
    longOpUntilMs = 0;
}

void TelemetryWriter::writeStatus(const ControlState& state, const std::string& sceneId,
                                  const std::string& sceneName) {
    ofJson status;
    const char* kind = state.lastControlEvent.kind == LastControlEvent::Kind::CC     ? "cc"
                       : state.lastControlEvent.kind == LastControlEvent::Kind::Note ? "note"
                                                                                     : "none";
    status["lastControl"] = {
        {"kind", kind},
        {"number", state.lastControlEvent.number},
        {"value", state.lastControlEvent.value01},
        {"ts", state.lastControlEvent.timeSeconds},
    };
    status["frameTimeMs"] = ofGetLastFrameTime() * 1000.0;
    status["fps"] = ofGetFrameRate();
    status["currentSceneId"] = sceneId;
    status["currentSceneName"] = sceneName;
    status["ts"] = ofGetElapsedTimef();
    // Which box the renderer actually came up on. Constant for the process,
    // but published every write so the web UI gets it from the first frame it
    // sees rather than needing a separate handshake -- and so a support
    // screenshot of Live mode carries the hardware tier with it.
    const auto& hw = livepi::platformInfo();
    status["hardware"] = {
        {"tier", hw.tierName},
        {"model", hw.model},
    };

    FILE* f = fopen(tmpPath.c_str(), "w");
    if (!f) return;  // tmpfs missing/mispermissioned -- telemetry is best-effort
    std::string body = status.dump();
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    rename(tmpPath.c_str(), path.c_str());
}
