#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "control/ControlState.h"

// Publishes renderer state for the backend: last-seen CC (Learn mode),
// frame time/fps (the editor's "this scene is heavy" indicator), and the
// current scene (Live mode's header). Written ~10x/sec to the tmpfs status
// path from app.json's ipc section, via temp-file + rename so the backend
// never reads a torn JSON.
//
// Also the wedge watchdog: the one confirmed v3d driver stall left the app
// alive-but-frozen (main thread stuck in a DRM ioctl), which systemd's
// Restart=on-failure can't see. A tiny side thread watches the main
// thread's heartbeat and abort()s if no frame completes for kWatchdogSecs
// -- turning a frozen-until-human-intervenes failure into a few seconds of
// freeze-frame and an automatic restart. See docs/architecture.md's decode
// budget section for the wedge itself.
class TelemetryWriter {
public:
    ~TelemetryWriter();

    void setup(const std::string& statusPath);
    // Call once per frame from the main loop.
    void update(const ControlState& state, const std::string& sceneId, const std::string& sceneName);

private:
    static constexpr double kWriteIntervalSecs = 0.1;
    static constexpr double kWatchdogSecs = 10.0;

    void writeStatus(const ControlState& state, const std::string& sceneId, const std::string& sceneName);

    std::string path;
    std::string tmpPath;
    double lastWriteSecs = 0.0;

    std::thread watchdog;
    std::atomic<bool> watchdogRunning{false};
    std::atomic<long long> lastHeartbeatMs{0};
};
