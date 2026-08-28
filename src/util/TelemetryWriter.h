// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
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

    // Bracket a KNOWN, bounded blocking operation on the main thread (a clip
    // preroll: ofGstVideoPlayer::load() builds+prerolls the pipeline
    // synchronously, which on a cold cache + hot/throttled SoC can exceed
    // kWatchdogSecs) so the watchdog waits graceSecs instead of mistaking the
    // stall for a wedge. beginLongOp is called right BEFORE the blocking call
    // (the thread can't heartbeat while inside it); endLongOp right after. If
    // the op itself hangs past graceSecs the watchdog still fires -> recovery.
    void beginLongOp(double graceSecs);
    void endLongOp();

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
    // While now < this, the watchdog holds off (a known long op is running).
    // 0 = no long op in flight. Written by the main thread, read by the watchdog.
    std::atomic<long long> longOpUntilMs{0};
};
