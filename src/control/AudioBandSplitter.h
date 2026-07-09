#pragma once

#include <cstddef>

// Splits a mono audio signal into low/mid/high envelope followers using a
// cascaded 3-way Linkwitz-Riley crossover (100 Hz / 2000 Hz) -- see
// docs/videosynth-backend.md's "Audio-band modulation" section. No FFT:
// two LR4 (24 dB/oct) splits followed by an asymmetric-EMA envelope per
// band is plenty to feel a kick/snare/hi-hat without spectral analysis.
// Fed directly from an ofBaseSoundInput::audioIn callback (audio thread),
// so process() is a plain streaming filter with no locks of its own --
// callers own whatever synchronization they need to read the envelopes
// back on the main thread (matching how currentAudioLevel/audioLevelMutex
// already works in MidiControlSource/PisoundControlSource).
class AudioBandSplitter {
public:
    void setup(float sampleRate);
    void process(const float* samples, size_t numFrames);

    // Peak-normalized 0..1 (relative to each band's own recent loudness --
    // see the normalization notes in the .cpp). The raw envelope scale is
    // an implementation detail no consumer should see.
    float getLow() const { return normalize(lowEnvelope, lowPeak); }
    float getMid() const { return normalize(midEnvelope, midPeak); }
    float getHigh() const { return normalize(highEnvelope, highPeak); }

    // Raw rolling peaks (pre-normalization scale) -- surfaced on the debug
    // overlay so normalization behavior is observable, not guessed at.
    float getLowPeakRaw() const { return lowPeak; }
    float getMidPeakRaw() const { return midPeak; }
    float getHighPeakRaw() const { return highPeak; }

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        // Direct Form II Transposed.
        float process(float x) {
            float y = b0 * x + z1;
            z1 = b1 * x + z2 - a1 * y;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // Two matched Q=0.707 Butterworth biquads in series == one
    // Linkwitz-Riley (24 dB/oct) stage -- it's specifically the squared
    // Butterworth response that makes a lowpass/highpass pair recombine
    // flat at the crossover point, which is the whole point of using LR
    // over a single higher-order filter here.
    struct LR4Stage {
        Biquad first;
        Biquad second;
        float process(float x) { return second.process(first.process(x)); }
    };

    static Biquad butterworthLowpass(float freq, float sampleRate);
    static Biquad butterworthHighpass(float freq, float sampleRate);
    static float updateEnvelope(float envelope, float rectified, float attackCoeff, float releaseCoeff);
    static float normalize(float envelope, float peak);

    // Stage 1 splits at 2000 Hz into "low+mid" and "high". Stage 2 takes
    // the "low+mid" output and splits again at 100 Hz into "low" and
    // "mid" -- a standard cascaded 3-way LR crossover.
    LR4Stage stage1Lowpass2000;
    LR4Stage stage1Highpass2000;
    LR4Stage stage2Lowpass100;
    LR4Stage stage2Highpass100;

    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float peakDecayCoeff = 1.0f;

    float lowEnvelope = 0.0f;
    float midEnvelope = 0.0f;
    float highEnvelope = 0.0f;
    float lowPeak = 0.0f;
    float midPeak = 0.0f;
    float highPeak = 0.0f;
};
