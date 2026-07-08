#include "MidiControlSource.h"

#include <cmath>

#include "ofLog.h"
#include "ofMath.h"
#include "ofSoundBuffer.h"
#include "ofUtils.h"
#include "util/Config.h"

void MidiControlSource::setup(const Config& config) {
    knobACcNumber = config.getInt("midi.knobA_cc", 21);
    knobBCcNumber = config.getInt("midi.knobB_cc", 22);

    midiIn.listInPorts();  // always visible in the log, regardless of open() outcome below

    std::string portName = config.getString("midi.port_name", "");
    bool opened = !portName.empty() && midiIn.openPort(portName);
    if (!portName.empty() && !opened) {
        ofLogWarning("MidiControlSource")
            << "Could not open MIDI port '" << portName << "', falling back to the first available port.";
    }
    if (!opened) {
        opened = midiIn.openPort(0);
    }
    if (!opened) {
        ofLogError("MidiControlSource") << "No MIDI input ports available.";
    }

    midiIn.addListener(this);
    midiIn.ignoreTypes(false, false, false);  // don't ignore clock/sysex/active-sense

    clock.start();

    auto devices = soundStream.getDeviceList();
    ofLogNotice("MidiControlSource") << devices.size() << " audio devices available:";
    for (const auto& device : devices) {
        ofLogNotice("MidiControlSource") << "  " << device.deviceID << ": " << device.name
                                          << " (in=" << device.inputChannels << ")"
                                          << (device.isDefaultInput ? " [default input]" : "");
    }

    ofSoundStreamSettings settings;
    settings.numInputChannels = 1;
    settings.numOutputChannels = 0;
    settings.sampleRate = 48000;
    settings.bufferSize = 256;
    settings.setInListener(this);

    std::string audioDeviceName = config.getString("audio.device_name", "");
    if (!audioDeviceName.empty()) {
        auto matches = soundStream.getMatchingDevices(audioDeviceName, 1);
        if (!matches.empty()) {
            settings.setInDevice(matches.front());
        } else {
            ofLogWarning("MidiControlSource")
                << "No audio input device matching '" << audioDeviceName << "', using the default.";
        }
    }
    soundStream.setup(settings);
}

void MidiControlSource::update() {
    state.lastButtonEvent = pendingButtonEvent;  // latched for exactly one update() cycle
    pendingButtonEvent = ButtonEvent::None;

    state.midiClockTicks = clock.getTotalTicks();
    state.beatInBar = clock.getBeatInBar();
    state.barNumber = clock.getBarNumber();
    state.bpmEstimate = clock.getBpmEstimate();
    state.clockPresent = clock.isClockPresent(ofGetElapsedTimef());
    state.healthy = true;  // a missing clock just means free-run, not a fault

    std::lock_guard<std::mutex> lock(audioLevelMutex);
    // One-pole smoothing so the level doesn't jitter frame to frame.
    state.audioLevel = state.audioLevel * 0.8f + currentAudioLevel * 0.2f;
}

void MidiControlSource::audioIn(ofSoundBuffer& buffer) {
    float sumSquares = 0.0f;
    for (size_t i = 0; i < buffer.getNumFrames(); ++i) {
        float sample = buffer.getSample(i, 0);
        sumSquares += sample * sample;
    }
    float rms = std::sqrt(sumSquares / std::max<size_t>(1, buffer.getNumFrames()));

    std::lock_guard<std::mutex> lock(audioLevelMutex);
    currentAudioLevel = rms;
}

void MidiControlSource::newMidiMessage(ofxMidiMessage& message) {
    switch (message.status) {
        case MIDI_TIME_CLOCK:
            clock.tick(ofGetElapsedTimef());
            break;
        case MIDI_START:
        case MIDI_CONTINUE:
            clock.reset();
            clock.start();
            break;
        case MIDI_STOP:
            clock.stop();
            break;
        case MIDI_CONTROL_CHANGE: {
            float normalized = message.value / 127.0f;
            state.ccValues[message.control] = normalized;
            ofLogNotice("MidiControlSource")
                << "CC " << message.control << " = " << normalized
                << "  (set midi.knobA_cc / midi.knobB_cc in app.local.json to map it)";
            if (message.control == knobACcNumber) {
                state.knobA = normalized * 2.0f - 1.0f;  // 0..1 -> -1..1, center-detent feel
            } else if (message.control == knobBCcNumber) {
                state.knobB = normalized;
            }
            break;
        }
        default:
            break;
    }
}

void MidiControlSource::keyPressed(int key) {
    // Same bindings as MockControlSource, so a knob that isn't CC-learned
    // yet (or a keyboard with no free assignable knobs at all) can still be
    // exercised from this same backend.
    switch (key) {
        case ' ':
            pendingButtonEvent = ButtonEvent::Click;
            break;
        case '[':
            state.knobA = ofClamp(state.knobA - 0.05f, -1.0f, 1.0f);
            break;
        case ']':
            state.knobA = ofClamp(state.knobA + 0.05f, -1.0f, 1.0f);
            break;
        case ',':
            state.knobB = ofClamp(state.knobB - 0.05f, 0.0f, 1.0f);
            break;
        case '.':
            state.knobB = ofClamp(state.knobB + 0.05f, 0.0f, 1.0f);
            break;
        default:
            break;
    }
}

void MidiControlSource::shutdown() {
    midiIn.closePort();
    soundStream.close();
}
