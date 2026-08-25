#include "ControlSource.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

#include "MidiControlSource.h"
#include "MockControlSource.h"
#include "PisoundControlSource.h"
#include "ofLog.h"
#include "ofSoundBuffer.h"
#include "ofSoundStream.h"
#include "ofxMidi.h"
#include "util/Config.h"

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool containsCI(const std::string& hay, const std::string& needle) {
    return lower(hay).find(lower(needle)) != std::string::npos;
}

// Resolve control_source=auto by probing what's actually connected. A Pisound
// (its ALSA MIDI port / capture device) wins; otherwise ANY real MIDI input or
// audio-capture device (a plugged-in USB controller / interface) selects the
// generic MIDI+audio source; with nothing attached we fall back to the mock.
// Identical logic on every platform -- desktop, Pi 4 + Pisound, or a Pi 5 with
// a USB interface (where Pisound may be off the table for thermal reasons).
std::string autoDetectKind() {
    ofxMidiIn midi;
    std::vector<std::string> ports;
    for (int i = 0; i < midi.getNumInPorts(); ++i) ports.push_back(midi.getInPortName(i));

    ofSoundStream stream;
    std::vector<ofSoundDevice> devices = stream.getDeviceList();

    ofLogNotice("ControlSource") << "auto-detect: " << ports.size() << " MIDI in port(s), "
                                 << devices.size() << " audio device(s)";
    for (const auto& p : ports) ofLogNotice("ControlSource") << "  midi: " << p;
    for (const auto& d : devices)
        ofLogNotice("ControlSource") << "  audio: " << d.name << " (in=" << d.inputChannels << ")";

    for (const auto& p : ports) if (containsCI(p, "pisound")) return "pisound";
    for (const auto& d : devices) if (containsCI(d.name, "pisound")) return "pisound";
    for (const auto& p : ports) if (!containsCI(p, "through")) return "midi";
    for (const auto& d : devices) if (d.inputChannels > 0) return "midi";
    return "mock";
}

}  // namespace

std::unique_ptr<ControlSource> createControlSource(const Config& config) {
    std::string kind = config.getString("control_source", "auto");
    if (kind == "auto") {
        kind = autoDetectKind();
        ofLogNotice("ControlSource") << "control_source=auto -> \"" << kind << "\"";
    }
    if (kind == "pisound") {
        return std::make_unique<PisoundControlSource>();
    }
    if (kind == "midi") {
        return std::make_unique<MidiControlSource>();
    }
    return std::make_unique<MockControlSource>();
}

void configureAudioInput(ofSoundStream& stream, ofSoundStreamSettings& settings,
                         const Config& config, const std::string& preferSubstr) {
    std::vector<ofSoundDevice> devices = stream.getDeviceList();
    ofLogNotice("ControlSource") << devices.size() << " audio device(s):";
    for (const auto& d : devices) {
        ofLogNotice("ControlSource") << "  [" << d.deviceID << "] " << d.name
                                     << " (in=" << d.inputChannels << ")"
                                     << (d.isDefaultInput ? " [default]" : "");
    }
    auto pick = [&](const std::function<bool(const ofSoundDevice&)>& ok) -> const ofSoundDevice* {
        for (const auto& d : devices)
            if (d.inputChannels > 0 && ok(d)) return &d;
        return nullptr;
    };
    const std::string configured = config.getString("audio.device_name", "");
    const ofSoundDevice* chosen = nullptr;
    if (!configured.empty())  // an explicit override always wins
        chosen = pick([&](const ofSoundDevice& d) { return containsCI(d.name, configured); });
    if (!chosen && !preferSubstr.empty())  // the source's own hardware, e.g. "pisound"
        chosen = pick([&](const ofSoundDevice& d) { return containsCI(d.name, preferSubstr); });
    if (!chosen)  // a connected USB capture device
        chosen = pick([&](const ofSoundDevice& d) { return containsCI(d.name, "usb"); });
    if (!chosen)  // any input at all (system default / built-in)
        chosen = pick([](const ofSoundDevice&) { return true; });
    if (chosen) {
        settings.setInDevice(*chosen);
        ofLogNotice("ControlSource") << "audio input -> \"" << chosen->name << "\"";
    } else {
        ofLogWarning("ControlSource") << "no audio input device found; using the backend default";
    }
}

bool openMidiInByName(ofxMidiIn& midiIn, const std::string& wanted) {
    midiIn.listInPorts();  // full list in the log regardless of the outcome below

    const int count = midiIn.getNumInPorts();

    // 1) First port whose name contains the wanted string (substring, not the
    //    exact match ofxMidiIn::openPort(name) does -- e.g. "pisound" matches
    //    ALSA's decorated "pisound:pisound MIDI PS-1a2b 24:0").
    int chosen = -1;
    if (!wanted.empty()) {
        for (int i = 0; i < count; ++i) {
            if (containsCI(midiIn.getInPortName(i), wanted)) { chosen = i; break; }
        }
    }
    // 2) Else a connected USB MIDI controller.
    if (chosen < 0) {
        for (int i = 0; i < count; ++i) {
            if (containsCI(midiIn.getInPortName(i), "usb")) { chosen = i; break; }
        }
    }
    // 3) Else the first port that is NOT the "Midi Through" loopback.
    if (chosen < 0) {
        for (int i = 0; i < count; ++i) {
            if (!containsCI(midiIn.getInPortName(i), "through")) { chosen = i; break; }
        }
    }
    if (chosen < 0) {
        ofLogWarning("ControlSource") << "No usable MIDI input port found (wanted '" << wanted << "').";
        return false;
    }
    if (!midiIn.openPort(chosen)) {
        ofLogWarning("ControlSource") << "Failed to open MIDI input port " << chosen << " ('"
                                      << midiIn.getInPortName(chosen) << "').";
        return false;
    }
    ofLogNotice("ControlSource") << "Opened MIDI input port " << chosen << ": '"
                                 << midiIn.getInPortName(chosen) << "' (wanted '" << wanted << "').";
    return true;
}

namespace {

// Append `value` only if it isn't already a candidate -- the ladders below
// are built from overlapping sources (what was requested, what the device
// advertises, the usual defaults) and retrying an identical combination just
// prints the same driver error twice.
void addCandidate(std::vector<size_t>& list, size_t value) {
    if (value == 0) return;
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

}  // namespace

int openAudioInput(ofSoundStream& stream, ofSoundStreamSettings& settings,
                   const Config& config, const std::string& preferSubstr) {
    configureAudioInput(stream, settings, config, preferSubstr);

    const size_t wantChannels = settings.numInputChannels > 0 ? settings.numInputChannels : 1;
    const size_t wantBuffer = settings.bufferSize > 0 ? settings.bufferSize : 256;

    // Channels: what was asked for, then what the chosen device actually
    // advertises (capped at 2 -- anything beyond a stereo pair just gets
    // averaged away in the downmix, so opening 8 inputs on an interface would
    // be pure overhead), then the other of 1/2.
    std::vector<size_t> channelLadder{wantChannels};
    if (const ofSoundDevice* dev = settings.getInDevice()) {
        if (dev->inputChannels > 0)
            addCandidate(channelLadder, std::min<size_t>(static_cast<size_t>(dev->inputChannels), 2));
    }
    addCandidate(channelLadder, 2);
    addCandidate(channelLadder, 1);

    // Buffer sizes: the configured one first (128 keeps the band envelopes
    // tight, which is the point on hardware that can take it), then the sizes
    // a USB interface is likely to insist on.
    std::vector<size_t> bufferLadder{wantBuffer};
    for (size_t b : {static_cast<size_t>(256), static_cast<size_t>(512), static_cast<size_t>(1024)})
        addCandidate(bufferLadder, b);

    for (size_t channels : channelLadder) {
        for (size_t bufferSize : bufferLadder) {
            settings.numInputChannels = channels;
            settings.bufferSize = bufferSize;
            ofLogVerbose("ControlSource") << "audio input: trying " << channels << "ch @ " << bufferSize
                                          << " frames";
            if (stream.setup(settings)) {
                ofLogNotice("ControlSource")
                    << "audio input open: " << channels << "ch @ " << bufferSize << " frames, "
                    << settings.sampleRate << " Hz"
                    << (channels == wantChannels && bufferSize == wantBuffer
                            ? ""
                            : "  (fell back from the configured "
                                  + std::to_string(wantChannels) + "ch @ "
                                  + std::to_string(wantBuffer) + ")");
                return static_cast<int>(channels);
            }
            // A refused setup can still leave the backend half-initialized;
            // close before the next attempt so failures don't accumulate.
            stream.close();
        }
    }

    // Loud, and specific about the consequence: the app keeps running and
    // looks fine, so without this line a dead audio input reads as "the
    // audio-reactive mappings don't work" with no clue where to look.
    ofLogError("ControlSource")
        << "could not open ANY audio input (tried " << channelLadder.size() << " channel count(s) x "
        << bufferLadder.size() << " buffer size(s)) -- audio level and the low/mid/high bands will "
        << "stay at 0, so every audio-reactive mapping is inert. Check `arecord -l`, and set "
        << "audio.device_name / audio.buffer_size in app.local.json if the device is picky.";
    return 0;
}

void downmixToMono(const ofSoundBuffer& buffer, std::vector<float>& out) {
    const size_t channels = std::max<size_t>(1, buffer.getNumChannels());
    // Trust the raw sample count over getNumFrames(): a short read would
    // otherwise walk off the end of the interleaved block.
    const size_t frames = std::min(buffer.getNumFrames(), buffer.getBuffer().size() / channels);
    out.resize(frames);
    if (channels == 1) {
        std::copy(buffer.getBuffer().begin(), buffer.getBuffer().begin() + frames, out.begin());
        return;
    }
    const float scale = 1.0f / static_cast<float>(channels);
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (size_t c = 0; c < channels; ++c) sum += buffer.getSample(i, c);
        out[i] = sum * scale;
    }
}
