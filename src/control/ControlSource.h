#pragma once

#include <memory>
#include <string>

#include "ControlState.h"

class Config;
class ofxMidiIn;

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

    // Live audio tuning pushed from settings.json (via SceneControlMap) each
    // frame. Default no-ops -- only the audio-capable sources react.
    virtual void setLevelSmoothing(float /*smoothing*/) {}
    virtual void setAutoGain(bool /*enabled*/) {}
};

// Reads config's "control_source" field ("mock" | "midi" | "pisound") and
// returns the matching backend. Defaults to "mock" if unset or
// unrecognized, so a misconfigured build never silently blocks on hardware
// that isn't there.
std::unique_ptr<ControlSource> createControlSource(const Config& config);

// Open the MIDI input port whose name CONTAINS `wanted` (case-insensitive),
// else the first port that is NOT ALSA's "Midi Through" loopback. Logs the port
// list and the choice. Returns true if a port was opened.
//
// This replaces `ofxMidiIn::openPort(name)` + its `openPort(0)` fallback, both
// of which are wrong on real hardware: openPort(name) is an EXACT match that
// never equals ALSA's decorated names (e.g. "pisound:pisound MIDI PS-1a2b 24:0"),
// so it always fell through to port 0 -- usually "Midi Through", which receives
// no external input. That silent mis-open is why MIDI Learn saw nothing while
// the Pisound's own activity LED still blinked.
bool openMidiInByName(ofxMidiIn& midiIn, const std::string& wanted);
