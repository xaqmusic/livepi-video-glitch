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
    updateBandPulses();

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

void MockControlSource::updateBandPulses() {
    constexpr uint32_t kQuarterNoteTicks = BeatClock::kPPQN;      // 1/4 note
    constexpr uint32_t kEighthNoteTicks = BeatClock::kPPQN / 2;   // 1/8 note
    constexpr float kPulseDecay = 0.9f;  // per update(), same feel as HSyncTearPass's beatSpike

    uint32_t ticks = clock.getTotalTicks();
    bool crossedQuarter = (lastPulseTicks / kQuarterNoteTicks) != (ticks / kQuarterNoteTicks);
    bool crossedEighth = (lastPulseTicks / kEighthNoteTicks) != (ticks / kEighthNoteTicks);
    lastPulseTicks = ticks;

    state.highBand = crossedEighth ? 1.0f : state.highBand * kPulseDecay;
    state.lowBand = crossedQuarter ? 1.0f : state.lowBand * kPulseDecay;

    // Backbeat: only the 2nd and 4th quarter note of the bar (0-indexed
    // beatInBar 1 and 3) pulse the mid band, everything else just decays.
    bool isBackbeat = crossedQuarter && (clock.getBeatInBar() == 1 || clock.getBeatInBar() == 3);
    state.midBand = isBackbeat ? 1.0f : state.midBand * kPulseDecay;
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
        // Keyboard-synthesized CC 21 in nine steps ('1' = 0.0 ... '9' = 1.0)
        // so the mapping resolver is exercisable with zero MIDI hardware --
        // the starter show maps CC 21 across all three post-effect params.
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            float value = (key - '1') / 8.0f;
            state.ccValues[21] = value;
            state.lastControlEvent = {LastControlEvent::Kind::CC, 21, value, ofGetElapsedTimef()};
            break;
        }
        default:
            break;
    }
}

void MockControlSource::shutdown() {}
