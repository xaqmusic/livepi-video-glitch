#pragma once

#include <mutex>

#include "BeatClock.h"
#include "ControlSource.h"
#include "ofSoundStream.h"
#include "ofxMidi.h"

// Desktop backend for testing against any real MIDI device (USB keyboard,
// controller, etc.) without needing Pisound hardware -- Phase 3 in
// docs/architecture.md. Shares its MIDI clock/CC handling with
// PisoundControlSource, and (Phase 4) also captures a real audioLevel via
// the desktop's own ofSoundStream as a stand-in for Pisound's audio input --
// skips only the Pisound-specific button-FIFO bridge, since a generic MIDI
// device has no physical scene button.
//
// Setup/detection workflow: on setup(), every available MIDI input port and
// audio device is logged (ofLogNotice) so you can see what's actually
// connected. It opens "midi.port_name"/"audio.device_name" from config if
// set, otherwise the first MIDI port / default audio device. Every incoming
// CC message is logged with its number and normalized value regardless of
// whether it's mapped to anything yet -- turn the knob you want to use, read
// its CC number off the console, then set midi.knobA_cc/midi.knobB_cc in
// app.local.json.
class MidiControlSource : public ControlSource, public ofxMidiListener, public ofBaseSoundInput {
public:
    void setup(const Config& config) override;
    void update() override;
    const ControlState& getState() const override { return state; }
    void shutdown() override;

    // ofxMidiListener
    void newMidiMessage(ofxMidiMessage& message) override;

    // ofBaseSoundInput -- runs on the audio thread, not the main thread.
    void audioIn(ofSoundBuffer& buffer) override;

    // Stand-in for the physical scene button, since a generic MIDI
    // keyboard doesn't have one wired up (forwarded from ofApp::keyPressed).
    void keyPressed(int key);

private:
    BeatClock clock;
    ControlState state;
    ofxMidiIn midiIn;

    int knobACcNumber = 21;
    int knobBCcNumber = 22;

    ofSoundStream soundStream;
    float currentAudioLevel = 0.0f;
    std::mutex audioLevelMutex;

    // keyPressed() runs from GLFW's pollEvents(), which oF calls *after*
    // update()/draw() each iteration -- so a click set directly on `state`
    // would be wiped by the next update() clearing it before SceneManager
    // ever reads it. This accumulator decouples the two: keyPressed() only
    // ever sets it, update() is the only thing that transfers it onto
    // `state` (for exactly one cycle) and clears it.
    ButtonEvent pendingButtonEvent = ButtonEvent::None;
};
