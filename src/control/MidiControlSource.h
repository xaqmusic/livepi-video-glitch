#pragma once

#include <map>

#include <mutex>

#include "AudioBandSplitter.h"
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
// whether it's mapped to anything yet.
//
// Two ways to map a knob: read its CC number off the console and set
// midi.knobA_cc/midi.knobB_cc in app.local.json (persists across restarts);
// or press 'a'/'b' then move the physical knob -- the next CC message that
// arrives is learned as that knob for the rest of this run (see keyPressed
// below). The console still logs the learned CC number so it can be copied
// into app.local.json to keep it next time.
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

    // Stand-in for the physical scene button (space Click, h Hold -- same
    // click/hold split PisoundControlSource gets from pisound-btn's FIFO);
    // a keyboard fallback for knobA/knobB ([/] and ,/. -- same bindings as
    // MockControlSource) for testing before both knobs are CC-learned, or on
    // a MIDI keyboard with no free assignable knobs at all; and 'a'/'b' to
    // arm CC-learn for knobA/knobB (see newMidiMessage). Forwarded from
    // ofApp::keyPressed.
    void keyPressed(int key);

private:
    enum class LearnTarget { None, KnobA, KnobB };

    // 14-bit CC pairing state (see newMidiMessage): raw MSB values and
    // arrival times, keyed by CC number.
    std::map<int, int> recentMsbRaw;
    std::map<int, double> recentMsbTime;

    BeatClock clock;
    ControlState state;
    ofxMidiIn midiIn;

    int knobACcNumber = 21;
    int knobBCcNumber = 22;
    LearnTarget pendingLearn = LearnTarget::None;

    ofSoundStream soundStream;
    float currentAudioLevel = 0.0f;
    AudioBandSplitter bandSplitter;
    float currentLowBand = 0.0f;
    float currentMidBand = 0.0f;
    float currentHighBand = 0.0f;
    std::mutex audioLevelMutex;

    // keyPressed() runs from GLFW's pollEvents(), which oF calls *after*
    // update()/draw() each iteration -- so a click set directly on `state`
    // would be wiped by the next update() clearing it before SceneManager
    // ever reads it. This accumulator decouples the two: keyPressed() only
    // ever sets it, update() is the only thing that transfers it onto
    // `state` (for exactly one cycle) and clears it.
    ButtonEvent pendingButtonEvent = ButtonEvent::None;
};
