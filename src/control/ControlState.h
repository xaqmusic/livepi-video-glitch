#pragma once

#include <cstdint>
#include <map>

enum class ButtonEvent {
    None,
    Click,
    Hold
};

// The most recent CC message seen, whenever it arrived -- what Learn mode
// binds against (the telemetry writer publishes it for the backend).
// timeSeconds is ofGetElapsedTimef() at arrival so consumers can tell a
// fresh wiggle from a stale latched value.
struct LastCcEvent {
    int number = -1;  // -1 = no CC ever seen
    float value01 = 0.0f;
    double timeSeconds = 0.0;
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
    LastCcEvent lastCcEvent;
    // DEPRECATED: knobA/knobB predate scene-scoped mappings (MappingResolver)
    // and are no longer read by any pass -- kept only so the keyboard
    // bindings in Mock/Midi sources still compile until removed.
    float knobA = 0.0f;
    float knobB = 0.0f;
    float audioLevel = 0.0f;        // smoothed 0..1
    float lowBand = 0.0f;            // smoothed 0..1, <100Hz envelope (kick/bass)
    float midBand = 0.0f;            // smoothed 0..1, 100Hz-2kHz envelope (snare/vocal)
    float highBand = 0.0f;          // smoothed 0..1, >2kHz envelope (hi-hat/cymbal)

    // Scene control
    ButtonEvent lastButtonEvent = ButtonEvent::None;

    // Diagnostics
    bool healthy = true;
};
