#include "PisoundControlSource.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>

#include "ofLog.h"
#include "ofSoundBuffer.h"
#include "ofUtils.h"
#include "util/Config.h"

void PisoundControlSource::setup(const Config& config) {
    knobACcNumber = config.getInt("midi.knobA_cc", 21);
    knobBCcNumber = config.getInt("midi.knobB_cc", 22);
    buttonFifoPath = config.getString("pisound.button_fifo", "/tmp/livepi-button.fifo");

    std::string portName = config.getString("midi.port_name", "pisound MIDI");
    if (!midiIn.openPort(portName)) {
        ofLogWarning("PisoundControlSource")
            << "Could not open MIDI port '" << portName << "', falling back to the first available port.";
        midiIn.openPort(0);
    }
    midiIn.addListener(this);
    midiIn.ignoreTypes(false, false, false);  // don't ignore clock/sysex/active-sense

    // Non-blocking read side of the button bridge FIFO;
    // scripts/pisound/advance-scene-btn.sh opens it O_WRONLY and writes one
    // byte per click/hold event.
    mkfifo(buttonFifoPath.c_str(), 0666);
    buttonFifoFd = open(buttonFifoPath.c_str(), O_RDONLY | O_NONBLOCK);
    if (buttonFifoFd < 0) {
        ofLogWarning("PisoundControlSource") << "Could not open button FIFO at " << buttonFifoPath;
    }

    ofSoundStreamSettings settings;
    settings.numInputChannels = 1;
    settings.numOutputChannels = 0;
    settings.sampleRate = 48000;
    settings.bufferSize = 256;
    settings.setInListener(this);
    soundStream.setup(settings);

    clock.start();
}

void PisoundControlSource::update() {
    state.lastButtonEvent = ButtonEvent::None;  // latched for exactly one update() cycle
    pollButtonFifo();
    pollAudioLevel();

    state.midiClockTicks = clock.getTotalTicks();
    state.beatInBar = clock.getBeatInBar();
    state.barNumber = clock.getBarNumber();
    state.bpmEstimate = clock.getBpmEstimate();
    state.clockPresent = clock.isClockPresent(ofGetElapsedTimef());
    state.healthy = state.clockPresent;
}

void PisoundControlSource::pollButtonFifo() {
    if (buttonFifoFd < 0) return;

    char buf[16];
    ssize_t n = read(buttonFifoFd, buf, sizeof(buf));
    if (n > 0) {
        // advance-scene-btn.sh writes 'c' for a click, 'h' for a hold; see
        // scripts/pisound/advance-scene-btn.sh for the exact protocol.
        char last = buf[n - 1];
        state.lastButtonEvent = (last == 'h') ? ButtonEvent::Hold : ButtonEvent::Click;
    }
}

void PisoundControlSource::pollAudioLevel() {
    std::lock_guard<std::mutex> lock(audioLevelMutex);
    // One-pole smoothing so the level doesn't jitter frame to frame.
    state.audioLevel = state.audioLevel * 0.8f + currentAudioLevel * 0.2f;
}

void PisoundControlSource::newMidiMessage(ofxMidiMessage& message) {
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

void PisoundControlSource::audioIn(ofSoundBuffer& buffer) {
    float sumSquares = 0.0f;
    for (size_t i = 0; i < buffer.getNumFrames(); ++i) {
        float sample = buffer.getSample(i, 0);
        sumSquares += sample * sample;
    }
    float rms = std::sqrt(sumSquares / std::max<size_t>(1, buffer.getNumFrames()));

    std::lock_guard<std::mutex> lock(audioLevelMutex);
    currentAudioLevel = rms;
}

void PisoundControlSource::shutdown() {
    midiIn.closePort();
    soundStream.close();
    if (buttonFifoFd >= 0) close(buttonFifoFd);
}
