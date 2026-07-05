#pragma once

#include <cstdint>
#include <map>

enum class ButtonEvent {
    None,
    Click,
    Hold
};

// Plain per-frame snapshot shared by both ControlSource backends. See
// docs/architecture.md ("Input abstraction") for the reasoning behind each
// field. lastButtonEvent is latched for exactly one update() cycle -- each
// backend clears it to None at the top of its own update() before applying
// any new events for that frame.
struct ControlState {
    // Clock / tempo
    uint32_t midiClockTicks = 0;
    int beatInBar = 0;
    int barNumber = 0;
    double bpmEstimate = 120.0;
    bool clockPresent = false;

    // Modulation
    std::map<int, float> ccValues;  // raw CC number -> normalized 0..1
    float knobA = 0.0f;             // configured CC, remapped to -1..1 (bidirectional)
    float knobB = 0.0f;             // configured CC, 0..1 (intensity)
    float audioLevel = 0.0f;        // smoothed 0..1

    // Scene control
    ButtonEvent lastButtonEvent = ButtonEvent::None;

    // Diagnostics
    bool healthy = true;
};
