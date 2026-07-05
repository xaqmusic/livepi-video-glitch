#pragma once

#include <cstdint>

// Shared MIDI-clock beat/bar counter. Both MockControlSource (synthetic
// ticks from a virtual timer) and PisoundControlSource (real MIDI 0xF8
// bytes) feed ticks into their own instance of this class, so beat math is
// identical between mock and real rather than two parallel implementations
// that could drift apart.
class BeatClock {
public:
    static constexpr int kPPQN = 24;
    static constexpr int kBeatsPerBar = 4;

    void start() { running = true; }
    void stop() { running = false; }
    void reset();

    // Call once per incoming MIDI clock tick (0xF8) or synthetic equivalent.
    // timestampSeconds should come from the same clock each call (e.g.
    // ofGetElapsedTimef()) so BPM estimation is meaningful.
    void tick(double timestampSeconds);

    uint32_t getTotalTicks() const { return totalTicks; }
    int getBeatInBar() const { return (totalTicks / kPPQN) % kBeatsPerBar; }
    int getBarNumber() const { return totalTicks / (kPPQN * kBeatsPerBar); }
    bool isRunning() const { return running; }
    double getBpmEstimate() const { return bpmEstimate; }

    // True if a tick has arrived within the last kClockTimeoutSeconds.
    // Backends use this to decide whether to free-run at a fallback BPM
    // instead of freezing when an external clock source drops out.
    bool isClockPresent(double nowSeconds) const;

private:
    static constexpr double kClockTimeoutSeconds = 1.0;

    uint32_t totalTicks = 0;
    bool running = false;
    double lastTickTime = -1.0;
    double bpmEstimate = 120.0;
};
