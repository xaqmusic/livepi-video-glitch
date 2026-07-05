#pragma once

#include <memory>

#include "ControlState.h"

class Config;

// Abstract source of everything the glitch engine reacts to: MIDI clock/CC,
// button events, and audio level. See docs/architecture.md for why this
// exists (MockControlSource for desktop dev, MidiControlSource for testing
// against a real MIDI device without Pisound hardware, PisoundControlSource
// for the real device) and ControlState.h for the data it produces.
class ControlSource {
public:
    virtual ~ControlSource() = default;

    virtual void setup(const Config& config) = 0;
    virtual void update() = 0;
    virtual const ControlState& getState() const = 0;
    virtual void shutdown() = 0;
};

// Reads config's "control_source" field ("mock" | "midi" | "pisound") and
// returns the matching backend. Defaults to "mock" if unset or
// unrecognized, so a misconfigured build never silently blocks on hardware
// that isn't there.
std::unique_ptr<ControlSource> createControlSource(const Config& config);
