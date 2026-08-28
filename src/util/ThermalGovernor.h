// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xaqmusic
#pragma once

#include <string>

// Keeps the show ALIVE when the Pi overheats instead of letting it throttle to
// a black screen: it steps the internal render scale down as the SoC heats past
// a threshold and back up as it cools. Fill-rate scales ~ scale^2, so one step
// (1.0 -> 0.66) is roughly a 55% GPU-load cut -- usually enough to pull back
// from the thermal edge while the show keeps running, softer but not dead.
//
// Reads the SoC temperature from sysfs (millidegrees). Where that file is
// absent or unreadable -- a desktop, or a board without it -- it's a no-op
// (scale stays 1.0). Poll it periodically from the main loop via update();
// it self-throttles and applies hysteresis so it can't flap mid-set.
class ThermalGovernor {
public:
    // Resolve the temperature source. LIVEPI_THERMAL_PATH overrides the sysfs
    // default (used to exercise the governor off-Pi).
    void setup();

    // Poll (throttled internally to a few seconds) and return the scale
    // multiplier to render at now (1.0 = full). Only changes across the
    // hot/cool thresholds.
    float update(float nowSecs);

    float scale() const;
    float tempC() const { return lastTempC; }
    bool active() const { return available; }
    int tier() const { return tierIndex; }

private:
    float readTempC() const;

    std::string tempPath;
    bool available = false;
    float lastTempC = 0.0f;
    float lastPollSecs = -1000.0f;
    float coolSinceSecs = -1.0f;  // when temp first dropped into the cool band
    int tierIndex = 0;
};
