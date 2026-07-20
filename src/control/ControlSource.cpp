#include "ControlSource.h"

#include <algorithm>
#include <cctype>

#include "MidiControlSource.h"
#include "MockControlSource.h"
#include "PisoundControlSource.h"
#include "ofLog.h"
#include "ofxMidi.h"
#include "util/Config.h"

std::unique_ptr<ControlSource> createControlSource(const Config& config) {
    std::string kind = config.getString("control_source", "mock");
    if (kind == "pisound") {
        return std::make_unique<PisoundControlSource>();
    }
    if (kind == "midi") {
        return std::make_unique<MidiControlSource>();
    }
    return std::make_unique<MockControlSource>();
}

bool openMidiInByName(ofxMidiIn& midiIn, const std::string& wanted) {
    midiIn.listInPorts();  // full list in the log regardless of the outcome below

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string wantLc = lower(wanted);
    const int count = midiIn.getNumInPorts();

    // 1) First port whose name contains the wanted string (substring, not the
    //    exact match ofxMidiIn::openPort(name) does).
    int chosen = -1;
    if (!wantLc.empty()) {
        for (int i = 0; i < count; ++i) {
            if (lower(midiIn.getInPortName(i)).find(wantLc) != std::string::npos) {
                chosen = i;
                break;
            }
        }
    }
    // 2) Else the first port that is NOT the "Midi Through" loopback.
    if (chosen < 0) {
        for (int i = 0; i < count; ++i) {
            if (lower(midiIn.getInPortName(i)).find("through") == std::string::npos) {
                chosen = i;
                break;
            }
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
