#include "ThermalGovernor.h"

#include <cstdlib>
#include <fstream>

#include "ofLog.h"

namespace {

// Coarse tiers, roughly full / ~480p-equiv / ~360p-equiv on a 720-tall panel.
// Fill-rate ~ scale^2, so each step is a big load cut. Add finer steps if a
// venue ever needs them.
constexpr float kTiers[] = {1.0f, 0.66f, 0.5f};
constexpr int kNumTiers = static_cast<int>(sizeof(kTiers) / sizeof(kTiers[0]));

constexpr float kHotC = 78.0f;         // at/above -> step DOWN a tier
constexpr float kCoolC = 66.0f;        // sustained below -> step UP a tier
constexpr float kCoolHoldSecs = 45.0f; // must stay cool this long before recovering
constexpr float kPollSecs = 3.0f;      // sysfs read cadence

}  // namespace

void ThermalGovernor::setup() {
    const char* override = std::getenv("LIVEPI_THERMAL_PATH");
    tempPath = override && *override ? override : "/sys/class/thermal/thermal_zone0/temp";
    std::ifstream probe(tempPath);
    available = probe.good();
    if (available) {
        lastTempC = readTempC();
        ofLogNotice("ThermalGovernor") << "watching " << tempPath << " (" << lastTempC << "C)";
    } else {
        ofLogNotice("ThermalGovernor") << "no temperature source at " << tempPath << " -- disabled";
    }
}

float ThermalGovernor::readTempC() const {
    std::ifstream f(tempPath);
    long milli = 0;
    if (f >> milli && milli > 0) return milli / 1000.0f;
    return -1.0f;  // unreadable / bogus
}

float ThermalGovernor::update(float nowSecs) {
    if (!available) return 1.0f;
    if (nowSecs - lastPollSecs < kPollSecs) return kTiers[tierIndex];
    lastPollSecs = nowSecs;

    float t = readTempC();
    if (t <= 0.0f) return kTiers[tierIndex];  // skip a bad read, hold the tier
    lastTempC = t;

    if (t >= kHotC && tierIndex < kNumTiers - 1) {
        tierIndex++;
        coolSinceSecs = -1.0f;
        ofLogWarning("ThermalGovernor")
            << "SoC " << t << "C -> reducing render scale to " << kTiers[tierIndex] << " to shed heat";
    } else if (t <= kCoolC && tierIndex > 0) {
        // Recover only after a sustained cool spell, so it can't oscillate.
        if (coolSinceSecs < 0.0f) {
            coolSinceSecs = nowSecs;
        } else if (nowSecs - coolSinceSecs >= kCoolHoldSecs) {
            tierIndex--;
            coolSinceSecs = -1.0f;
            ofLogNotice("ThermalGovernor")
                << "SoC " << t << "C cooled -> restoring render scale to " << kTiers[tierIndex];
        }
    } else {
        coolSinceSecs = -1.0f;  // in the middle band: reset the recovery timer
    }
    return kTiers[tierIndex];
}

float ThermalGovernor::scale() const { return kTiers[tierIndex]; }
