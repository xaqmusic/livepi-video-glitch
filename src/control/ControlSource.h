#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ControlState.h"

class Config;
class ofxMidiIn;
class ofSoundBuffer;
class ofSoundStream;
class ofSoundStreamSettings;

// Abstract source of everything the glitch engine reacts to: MIDI clock/CC,
// button events, and audio level. See docs/architecture.md for why this
// exists (MockControlSource for desktop dev, MidiControlSource for testing
// against a real MIDI device without Pisound hardware, PisoundControlSource
// for the real device) and ControlState.h for the data it produces.
class ControlSource {
public:
    virtual ~ControlSource() = default;

    virtual void setup(const Config& config) = 0;
    virtual void update() = 0;
    virtual const ControlState& getState() const = 0;
    virtual void shutdown() = 0;

    // Live audio tuning pushed from settings.json (via SceneControlMap) each
    // frame. Default no-ops -- only the audio-capable sources react.
    virtual void setLevelSmoothing(float /*smoothing*/) {}
    virtual void setAutoGain(bool /*enabled*/) {}
};

// Reads config's "control_source" field ("mock" | "midi" | "pisound") and
// returns the matching backend. Defaults to "mock" if unset or
// unrecognized, so a misconfigured build never silently blocks on hardware
// that isn't there.
std::unique_ptr<ControlSource> createControlSource(const Config& config);

// Open the MIDI input port whose name CONTAINS `wanted` (case-insensitive),
// else the first port that is NOT ALSA's "Midi Through" loopback. Logs the port
// list and the choice. Returns true if a port was opened.
//
// This replaces `ofxMidiIn::openPort(name)` + its `openPort(0)` fallback, both
// of which are wrong on real hardware: openPort(name) is an EXACT match that
// never equals ALSA's decorated names (e.g. "pisound:pisound MIDI PS-1a2b 24:0"),
// so it always fell through to port 0 -- usually "Midi Through", which receives
// no external input. That silent mis-open is why MIDI Learn saw nothing while
// the Pisound's own activity LED still blinked.
bool openMidiInByName(ofxMidiIn& midiIn, const std::string& wanted);

// Choose the audio-capture device for a control source, in priority order:
// an explicit audio.device_name config override, then the source's own hardware
// (preferSubstr, e.g. "pisound"), then any connected USB capture device, then
// any input at all (system default). Sets settings.setInDevice() and logs the
// full device list + the choice. No match leaves the backend default in place.
void configureAudioInput(ofSoundStream& stream, ofSoundStreamSettings& settings,
                         const Config& config, const std::string& preferSubstr);

// Pick the capture device (configureAudioInput above), then actually OPEN the
// stream, retrying across channel counts and buffer sizes until one
// combination is accepted. Returns the number of input channels that opened,
// or 0 if none did.
//
// Why a ladder and not one setup() call: with Pisound there was exactly one
// device and it took whatever was asked for, so a single mono 128-frame
// request always worked. Generic USB interfaces are not so accommodating --
// plenty of them are capture-STEREO-only (a 2-in interface refuses a 1-channel
// open through ALSA's hw: device, which is what RtAudio uses) and plenty want
// at least 256 frames. Either refusal used to leave a stream that never
// started, with the return value dropped on the floor: every audio-reactive
// mapping silently sat at zero, with nothing in the log to say why. The tier
// that opens is reported back so the caller can downmix to mono.
int openAudioInput(ofSoundStream& stream, ofSoundStreamSettings& settings,
                   const Config& config, const std::string& preferSubstr);

// Fold an interleaved N-channel capture buffer down to a contiguous mono
// block in `out` (averaged across channels, so a signal on either side of a
// stereo pair is heard). Both the RMS level and AudioBandSplitter want a
// plain mono stream; before openAudioInput's fallbacks, feeding them a
// stereo buffer as though it were mono read L,R,L,R as consecutive samples --
// an octave-shifted, half-length garble of the real signal.
void downmixToMono(const ofSoundBuffer& buffer, std::vector<float>& out);
