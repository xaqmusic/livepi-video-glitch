#include "MidiControlSource.h"

#include <algorithm>
#include <cmath>

#include "ofLog.h"
#include "ofMath.h"
#include "ofSoundBuffer.h"
#include "ofUtils.h"
#include "util/Config.h"

void MidiControlSource::setup(const Config& config) {
    knobACcNumber = config.getInt("midi.knobA_cc", 21);
    knobBCcNumber = config.getInt("midi.knobB_cc", 22);

    // Empty wanted-name -> auto-pick the first non-"Midi Through" port (a USB
    // controller); see openMidiInByName for why openPort(name)/openPort(0) were
    // both wrong.
    openMidiInByName(midiIn, config.getString("midi.port_name", ""));
    midiIn.addListener(this);
    midiIn.ignoreTypes(false, false, false);  // don't ignore clock/sysex/active-sense

    clock.start();

    // Low-latency audio input, tunable in app.json (see PisoundControlSource).
    levelSmoothing = std::clamp(config.getFloat("audio.level_smoothing", 0.6f), 0.0f, 0.98f);
    ofSoundStreamSettings settings;
    // Mono by default; audio.input_channels=2 captures a stereo pair and folds
    // it down (downmixToMono), which is what you want when the signal is on
    // the interface's second input or spread across both. openAudioInput
    // falls back across counts anyway if the device refuses this one.
    settings.numInputChannels = std::max(1, config.getInt("audio.input_channels", 1));
    settings.numOutputChannels = 0;
    settings.sampleRate = config.getInt("audio.sample_rate", 48000);
    settings.bufferSize = config.getInt("audio.buffer_size", 128);
    settings.numBuffers = config.getInt("audio.num_buffers", 2);
    settings.setInListener(this);
    // No source-specific hardware to prefer: an explicit override > a connected
    // USB capture device > the system default.
    openInputChannels = openAudioInput(soundStream, settings, config, "");
    // settings.bufferSize is what openAudioInput SETTLED on, not what was
    // requested -- reserve against that so the audio thread's mono fold never
    // allocates in the callback.
    monoScratch.reserve(settings.bufferSize);
    bandSplitter.setup(static_cast<float>(settings.sampleRate));
}

void MidiControlSource::setLevelSmoothing(float smoothing) {
    levelSmoothing = std::clamp(smoothing, 0.0f, 0.98f);
}

void MidiControlSource::setAutoGain(bool enabled) {
    bandSplitter.setAutoGain(enabled);
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
    // One-pole smoothing so the overall level doesn't jitter frame to frame.
    state.audioLevel = state.audioLevel * levelSmoothing + currentAudioLevel * (1.0f - levelSmoothing);
    state.lowBand = currentLowBand;
    state.midBand = currentMidBand;
    state.highBand = currentHighBand;
    state.lowPeakRaw = currentLowPeakRaw;
    state.midPeakRaw = currentMidPeakRaw;
    state.highPeakRaw = currentHighPeakRaw;
}

void MidiControlSource::audioIn(ofSoundBuffer& buffer) {
    // Whatever channel count actually opened, fold it to mono first: both the
    // RMS below and AudioBandSplitter want a contiguous mono block, and a
    // generic USB interface may well have opened as stereo (see
    // openAudioInput). This is a no-op copy in the mono case.
    downmixToMono(buffer, monoScratch);

    float sumSquares = 0.0f;
    for (float sample : monoScratch) sumSquares += sample * sample;
    float rms = std::sqrt(sumSquares / std::max<size_t>(1, monoScratch.size()));

    bandSplitter.process(monoScratch.data(), monoScratch.size());

    std::lock_guard<std::mutex> lock(audioLevelMutex);
    currentAudioLevel = rms;
    currentLowBand = bandSplitter.getLow();
    currentMidBand = bandSplitter.getMid();
    currentHighBand = bandSplitter.getHigh();
    currentLowPeakRaw = bandSplitter.getLowPeakRaw();
    currentMidPeakRaw = bandSplitter.getMidPeakRaw();
    currentHighPeakRaw = bandSplitter.getHighPeakRaw();
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
        case MIDI_NOTE_ON:
        case MIDI_NOTE_OFF: {
            // Notes are bindable triggers just like CCs: press drives the
            // mapping by velocity, release drops it to 0 -- momentary-button
            // semantics (a stutter punch on a drum pad, say).
            float velocity = message.status == MIDI_NOTE_ON ? message.velocity / 127.0f : 0.0f;
            state.noteValues[message.pitch] = velocity;
            state.lastControlEvent = {LastControlEvent::Kind::Note, message.pitch, velocity, ofGetElapsedTimef()};
            break;
        }
        case MIDI_CONTROL_CHANGE: {
            // 14-bit CC pairing: hi-res controllers send a coarse MSB on CC
            // N and a fine LSB on CC N+32 (observed live: CC 3 + CC 35,
            // CC 71 + CC 103). Without pairing, the LSB half looks like an
            // independent knob that sweeps the whole 0..1 range every
            // 1/128th of physical travel -- and Learn almost always caught
            // that half, since the LSB arrives last in each pair. If this
            // CC's N-32 partner spoke recently, fold this value into the
            // partner as its fine half and never surface the LSB number as
            // a control of its own.
            double nowSecs = ofGetElapsedTimef();
            if (message.control >= 32) {
                auto msbIt = recentMsbRaw.find(message.control - 32);
                auto timeIt = recentMsbTime.find(message.control - 32);
                if (msbIt != recentMsbRaw.end() && timeIt != recentMsbTime.end()
                    && nowSecs - timeIt->second < 0.5) {
                    int primary = message.control - 32;
                    float combined = (msbIt->second * 128 + message.value) / 16383.0f;
                    state.ccValues[primary] = combined;
                    state.lastControlEvent = {LastControlEvent::Kind::CC, primary, combined, nowSecs};
                    break;
                }
            }
            recentMsbRaw[message.control] = message.value;
            recentMsbTime[message.control] = nowSecs;

            float normalized = message.value / 127.0f;
            state.ccValues[message.control] = normalized;
            state.lastControlEvent = {LastControlEvent::Kind::CC, message.control, normalized, nowSecs};
            // Per-message, so verbose-only (--verbose): this floods the console
            // on every knob turn. Web-UI MIDI-learn is the primary mapping path
            // now; the console readout is a fallback for --verbose sessions.
            ofLogVerbose("MidiControlSource")
                << "CC " << message.control << " = " << normalized
                << "  (set midi.knobA_cc / midi.knobB_cc in app.local.json to map it, or press 'a'/'b' to learn it)";

            if (pendingLearn == LearnTarget::KnobA) {
                knobACcNumber = message.control;
                pendingLearn = LearnTarget::None;
                ofLogNotice("MidiControlSource")
                    << "knobA learned: CC " << knobACcNumber << " (set midi.knobA_cc to this in app.local.json"
                    << " to keep it next run)";
            } else if (pendingLearn == LearnTarget::KnobB) {
                knobBCcNumber = message.control;
                pendingLearn = LearnTarget::None;
                ofLogNotice("MidiControlSource")
                    << "knobB learned: CC " << knobBCcNumber << " (set midi.knobB_cc to this in app.local.json"
                    << " to keep it next run)";
            }

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
    switch (key) {
        case ' ':
            pendingButtonEvent = ButtonEvent::Click;
            break;
        case 'h':
            pendingButtonEvent = ButtonEvent::Hold;
            break;
        // Keyboard fallback for knobA/knobB (same bindings as
        // MockControlSource), so a knob that isn't CC-learned yet -- or a
        // MIDI keyboard with no free assignable knobs at all -- can still be
        // exercised from this same backend.
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
        // Arm CC-learn: the next CC message that arrives is assigned as that
        // knob (handled in newMidiMessage), instead of needing to read its
        // number off the console and edit app.local.json by hand.
        case 'a':
            pendingLearn = LearnTarget::KnobA;
            ofLogNotice("MidiControlSource") << "Learning knobA -- move the knob you want to assign...";
            break;
        case 'b':
            pendingLearn = LearnTarget::KnobB;
            ofLogNotice("MidiControlSource") << "Learning knobB -- move the knob you want to assign...";
            break;
        default:
            break;
    }
}

void MidiControlSource::shutdown() {
    midiIn.closePort();
    soundStream.close();
}
