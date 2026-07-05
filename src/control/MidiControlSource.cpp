#include "MidiControlSource.h"

#include "ofLog.h"
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
    if (key == ' ') {
        pendingButtonEvent = ButtonEvent::Click;
    }
}

void MidiControlSource::shutdown() {
    midiIn.closePort();
}
