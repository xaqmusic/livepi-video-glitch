#include "BeatClock.h"

void BeatClock::reset() {
    totalTicks = 0;
    lastTickTime = -1.0;
}

void BeatClock::tick(double timestampSeconds) {
    if (lastTickTime >= 0.0) {
        double delta = timestampSeconds - lastTickTime;
        if (delta > 0.0) {
            double instantBpm = 60.0 / (delta * kPPQN);
            // Exponential moving average smooths jitter between individual
            // ticks -- a raw instantaneous BPM reading is too noisy to
            // display or drive anything from directly.
            constexpr double kSmoothing = 0.1;
            bpmEstimate += kSmoothing * (instantBpm - bpmEstimate);
        }
    }
    lastTickTime = timestampSeconds;
    ++totalTicks;
}

bool BeatClock::isClockPresent(double nowSeconds) const {
    return lastTickTime >= 0.0 && (nowSeconds - lastTickTime) < kClockTimeoutSeconds;
}
