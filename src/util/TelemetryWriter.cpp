#include "TelemetryWriter.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "ofAppRunner.h"
#include "ofFileUtils.h"
#include "ofJson.h"
#include "ofLog.h"
#include "ofUtils.h"

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

    lastHeartbeatMs = nowMs();
    watchdogRunning = true;
    watchdog = std::thread([this] {
        while (watchdogRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            long long silentMs = nowMs() - lastHeartbeatMs.load();
            if (watchdogRunning && silentMs > static_cast<long long>(kWatchdogSecs * 1000)) {
                // Frozen main thread (the rare v3d wedge) -- die loudly so
                // systemd's Restart=on-failure brings the show back instead
                // of freezing until a human notices.
                ofLogFatalError("TelemetryWriter")
                    << "No frame completed in " << (silentMs / 1000) << "s -- watchdog abort.";
                std::abort();
            }
        }
    });
}

void TelemetryWriter::update(const ControlState& state, const std::string& sceneId, const std::string& sceneName) {
    lastHeartbeatMs = nowMs();

    double now = ofGetElapsedTimef();
    if (now - lastWriteSecs < kWriteIntervalSecs) return;
    lastWriteSecs = now;
    writeStatus(state, sceneId, sceneName);
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

    FILE* f = fopen(tmpPath.c_str(), "w");
    if (!f) return;  // tmpfs missing/mispermissioned -- telemetry is best-effort
    std::string body = status.dump();
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    rename(tmpPath.c_str(), path.c_str());
}
