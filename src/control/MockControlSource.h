#pragma once

#include "BeatClock.h"
#include "ControlSource.h"

// Desktop development backend. Generates a synthetic 24-PPQN MIDI clock at a
// configurable BPM and maps keyboard keys to the button/knob inputs the real
// PisoundControlSource would otherwise supply, so the glitch engine and
// scene logic can be exercised end-to-end with nothing but a laptop.
//
// Keys (forwarded from ofApp::keyPressed while the window has focus):
//   space        button Click
//   [ / ]        knobA down / up (bidirectional, centered at 0)
//   , / .        knobB down / up (0..1)
//   - / =        tempo down / up
class MockControlSource : public ControlSource {
public:
    void setup(const Config& config) override;
    void update() override;
    const ControlState& getState() const override { return state; }
    void shutdown() override;

    void keyPressed(int key);

private:
    void applyClockTicksSince(double nowSeconds);

    BeatClock clock;
    ControlState state;
    double bpm = 120.0;
    double lastTickTime = 0.0;
};
