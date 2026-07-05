#pragma once

#include "BeatClock.h"
#include "ControlSource.h"
#include "ofxMidi.h"

// Desktop backend for testing against any real MIDI device (USB keyboard,
// controller, etc.) without needing Pisound hardware -- Phase 3 in
// docs/architecture.md. Shares its MIDI clock/CC handling with
// PisoundControlSource, but skips the Pisound-specific audio-input and
// button-FIFO bridge, since a generic MIDI device has neither.
//
// Setup/detection workflow: on setup(), every available MIDI input port is
// logged (ofLogNotice) so you can see what's actually connected. It opens
// "midi.port_name" from config if set (exact name match), otherwise the
// first available port. Every incoming CC message is logged with its
// number and normalized value regardless of whether it's mapped to
// anything yet -- turn the knob you want to use, read its CC number off
// the console, then set midi.knobA_cc/midi.knobB_cc in app.local.json.
class MidiControlSource : public ControlSource, public ofxMidiListener {
public:
    void setup(const Config& config) override;
    void update() override;
    const ControlState& getState() const override { return state; }
    void shutdown() override;

    // ofxMidiListener
    void newMidiMessage(ofxMidiMessage& message) override;

    // Stand-in for the physical scene button, since a generic MIDI
    // keyboard doesn't have one wired up (forwarded from ofApp::keyPressed).
    void keyPressed(int key);

private:
    BeatClock clock;
    ControlState state;
    ofxMidiIn midiIn;

    int knobACcNumber = 21;
    int knobBCcNumber = 22;
};
