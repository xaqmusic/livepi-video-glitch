#pragma once

#include <mutex>
#include <string>

#include "BeatClock.h"
#include "ControlSource.h"
#include "ofSoundStream.h"
#include "ofxMidi.h"

// Raspberry Pi + Pisound backend. Reads MIDI clock/CC from Pisound's ALSA
// MIDI port via ofxMidi, live audio input level via ofSoundStream against
// Pisound's ALSA capture device, and button events via a FIFO that
// scripts/pisound/advance-scene-btn.sh writes to when pisound-btn fires a
// configured click pattern (see docs/pisound-hardware-notes.md).
//
// Pisound's onboard GAIN/VOLUME knobs are fixed-function analog audio trims
// with no software visibility (verified against Blokas' docs) -- they are
// deliberately NOT part of this class. knobA/knobB come from configured MIDI
// CC numbers instead, same as any external synth's assignable knobs.
class PisoundControlSource : public ControlSource, public ofxMidiListener, public ofBaseSoundInput {
public:
    void setup(const Config& config) override;
    void update() override;
    const ControlState& getState() const override { return state; }
    void shutdown() override;

    // ofxMidiListener
    void newMidiMessage(ofxMidiMessage& message) override;

    // ofBaseSoundInput -- runs on the audio thread, not the main thread.
    void audioIn(ofSoundBuffer& buffer) override;

private:
    void pollButtonFifo();
    void pollAudioLevel();

    BeatClock clock;
    ControlState state;
    ofxMidiIn midiIn;

    int knobACcNumber = 21;
    int knobBCcNumber = 22;

    std::string buttonFifoPath;
    int buttonFifoFd = -1;

    ofSoundStream soundStream;
    float currentAudioLevel = 0.0f;
    std::mutex audioLevelMutex;
};
