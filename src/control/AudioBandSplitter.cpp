#include "AudioBandSplitter.h"

#include <cmath>

namespace {
constexpr float kLowMidCrossoverHz = 100.0f;
constexpr float kMidHighCrossoverHz = 2000.0f;
// Fast attack so a kick/snare/hi-hat transient registers immediately,
// slower release so the resulting pulse stays visually readable instead
// of flickering frame to frame.
constexpr float kAttackTimeSeconds = 0.005f;
constexpr float kReleaseTimeSeconds = 0.15f;
// Adaptive normalization: raw rectified envelopes sit at 0.001..0.1 for
// real mic/line levels -- far too small to drive a 0..1 param visibly
// (found when "binding audio had no effect" with a real USB mic). Each
// band normalizes against its own rolling peak, which decays by half
// every ~20s so the scale adapts to the room/set volume, with a noise
// floor so silence outputs 0 instead of amplifying hiss to full scale.
constexpr float kPeakHalfLifeSeconds = 20.0f;
// The floor only exists to keep SILENCE from normalizing hiss to full
// scale -- it must sit just above the mic's actual noise, not above quiet
// music. Measured silent-room envelopes on the real USB mic are ~5e-5;
// the original guess of 3e-3 was 60x that and zeroed everything but loud
// input (the "pulse only works when very loud" bug).
constexpr float kNoiseFloor = 0.0005f;
// Beats rarely re-touch the exact recent maximum; treating anything
// within 70% of the peak as full deflection keeps pulses punchy at every
// volume instead of only on the single loudest hit.
constexpr float kHeadroom = 0.7f;
}  // namespace

void AudioBandSplitter::setup(float sampleRate) {
    stage1Lowpass2000.first = butterworthLowpass(kMidHighCrossoverHz, sampleRate);
    stage1Lowpass2000.second = stage1Lowpass2000.first;
    stage1Highpass2000.first = butterworthHighpass(kMidHighCrossoverHz, sampleRate);
    stage1Highpass2000.second = stage1Highpass2000.first;

    stage2Lowpass100.first = butterworthLowpass(kLowMidCrossoverHz, sampleRate);
    stage2Lowpass100.second = stage2Lowpass100.first;
    stage2Highpass100.first = butterworthHighpass(kLowMidCrossoverHz, sampleRate);
    stage2Highpass100.second = stage2Highpass100.first;

    attackCoeff = std::exp(-1.0f / (kAttackTimeSeconds * sampleRate));
    releaseCoeff = std::exp(-1.0f / (kReleaseTimeSeconds * sampleRate));
    peakDecayCoeff = std::exp(std::log(0.5f) / (kPeakHalfLifeSeconds * sampleRate));
}

AudioBandSplitter::Biquad AudioBandSplitter::butterworthLowpass(float freq, float sampleRate) {
    // RBJ Audio EQ Cookbook, Q = 0.70710678 (Butterworth).
    float omega = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * 0.70710678f);

    float a0 = 1.0f + alpha;
    Biquad bq;
    bq.b0 = ((1.0f - cosOmega) / 2.0f) / a0;
    bq.b1 = (1.0f - cosOmega) / a0;
    bq.b2 = bq.b0;
    bq.a1 = (-2.0f * cosOmega) / a0;
    bq.a2 = (1.0f - alpha) / a0;
    return bq;
}

AudioBandSplitter::Biquad AudioBandSplitter::butterworthHighpass(float freq, float sampleRate) {
    float omega = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * 0.70710678f);

    float a0 = 1.0f + alpha;
    Biquad bq;
    bq.b0 = ((1.0f + cosOmega) / 2.0f) / a0;
    bq.b1 = -(1.0f + cosOmega) / a0;
    bq.b2 = bq.b0;
    bq.a1 = (-2.0f * cosOmega) / a0;
    bq.a2 = (1.0f - alpha) / a0;
    return bq;
}

float AudioBandSplitter::updateEnvelope(float envelope, float rectified, float attackCoeff, float releaseCoeff) {
    float coeff = rectified > envelope ? attackCoeff : releaseCoeff;
    return coeff * envelope + (1.0f - coeff) * rectified;
}

void AudioBandSplitter::process(const float* samples, size_t numFrames) {
    for (size_t i = 0; i < numFrames; ++i) {
        float x = samples[i];

        float lowMid = stage1Lowpass2000.process(x);
        float high = stage1Highpass2000.process(x);
        float low = stage2Lowpass100.process(lowMid);
        float mid = stage2Highpass100.process(lowMid);

        lowEnvelope = updateEnvelope(lowEnvelope, std::fabs(low), attackCoeff, releaseCoeff);
        midEnvelope = updateEnvelope(midEnvelope, std::fabs(mid), attackCoeff, releaseCoeff);
        highEnvelope = updateEnvelope(highEnvelope, std::fabs(high), attackCoeff, releaseCoeff);

        lowPeak = std::max(lowEnvelope, lowPeak * peakDecayCoeff);
        midPeak = std::max(midEnvelope, midPeak * peakDecayCoeff);
        highPeak = std::max(highEnvelope, highPeak * peakDecayCoeff);
    }
}

float AudioBandSplitter::normalize(float envelope, float peak) {
    if (peak < kNoiseFloor) return 0.0f;
    float v = envelope / (peak * kHeadroom);
    return v > 1.0f ? 1.0f : v;
}
