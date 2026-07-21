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
