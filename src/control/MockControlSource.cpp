#include "MockControlSource.h"

#include "ofMain.h"
#include "util/Config.h"

void MockControlSource::setup(const Config& config) {
    bpm = config.getFloat("mock.bpm", 120.0f);
    lastTickTime = ofGetElapsedTimef();
    clock.start();
    state.bpmEstimate = bpm;
}

void MockControlSource::update() {
    state.lastButtonEvent = pendingButtonEvent;  // latched for exactly one update() cycle
    pendingButtonEvent = ButtonEvent::None;

    applyClockTicksSince(ofGetElapsedTimef());

    state.midiClockTicks = clock.getTotalTicks();
    state.beatInBar = clock.getBeatInBar();
    state.barNumber = clock.getBarNumber();
    state.bpmEstimate = clock.getBpmEstimate();
    state.clockPresent = true;  // the mock clock never "drops out"
    state.healthy = true;
}

void MockControlSource::applyClockTicksSince(double nowSeconds) {
    double secondsPerTick = 60.0 / (bpm * BeatClock::kPPQN);
    while (nowSeconds - lastTickTime >= secondsPerTick) {
        lastTickTime += secondsPerTick;
        clock.tick(lastTickTime);
    }
}

void MockControlSource::keyPressed(int key) {
    switch (key) {
        case ' ':
            pendingButtonEvent = ButtonEvent::Click;
            break;
        case 'h':
            pendingButtonEvent = ButtonEvent::Hold;
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
        case '-':
            bpm = ofClamp(bpm - 2.0, 40.0, 300.0);
            break;
        case '=':
            bpm = ofClamp(bpm + 2.0, 40.0, 300.0);
            break;
        default:
            break;
    }
}

void MockControlSource::shutdown() {}
