// ============================================================================
// ÜBERSAW — JP-8000 Supersaw Engine Implementation
// ============================================================================

#include "supersaw.h"
#include <cmath>

// The original detune table from the TC170C140 firmware.
// These values are asymmetric — the negative offsets are slightly larger
// than the positive ones. This asymmetry was confirmed by both the
// reverse engineering and Adam Szabo's earlier spectral analysis.
constexpr int32_t SuperSaw::kDetuneTable[NUM_OSCS];

// ============================================================================
// Lifecycle
// ============================================================================

void SuperSaw::Init(float sample_rate) {
    sample_rate_ = sample_rate;

    // Initialize all oscillators to zero phase
    for (int i = 0; i < NUM_OSCS; i++) {
        saw_[i] = 0;
        saw_f_[i] = 0.0f;
    }

    // Default parameters
    SetFreq(440.0f);
    SetDetune(0.5f);
    SetMix(1.0f);
    SetFilterOffset(1.0f);

    // Initialize HPF
    hpf_.Reset();
    hpf_.SetFreq(freq_hz_ * filter_offset_, sample_rate_);
}

// ============================================================================
// Parameter setters
// ============================================================================

void SuperSaw::SetFreq(float freq_hz) {
    freq_hz_ = freq_hz;
    pitch_inc_ = FreqToPhaseInc(freq_hz);

    // Update pitch-tracked HPF: cutoff follows the fundamental
    hpf_.SetFreq(freq_hz_ * filter_offset_, sample_rate_);
}

void SuperSaw::SetDetune(float detune) {
    // Apply Szabo 11th-order polynomial curve to shape the detune knob response.
    // The JP-8000 uses a non-linear curve: gentle at low settings, aggressive
    // at the top. Coefficients from Adam Szabo's thesis "How to Emulate the
    // Super Saw", confirmed by 39C3 reverse engineering / JE-8086 emulator.
    //
    // Polynomial: detune(x) = c0*x^11 + c1*x^10 + ... + c10*x + c11
    // Input x in [0,1], output ≈ [0.003, 1.0]
    float x = detune;
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    // Horner's method for numerical stability and efficiency
    float shaped = ((((((((((10028.7312891634f * x
                     - 50818.8652045924f) * x
                     + 111363.4808729368f) * x
                     - 138150.6761080548f) * x
                     + 106649.6679158292f) * x
                     - 53046.9642751875f) * x
                     + 17019.9518580080f) * x
                     - 3425.0836591318f) * x
                     + 404.2703938388f) * x
                     - 24.1878824391f) * x
                     + 0.6717417634f) * x
                     + 0.0030115596f;

    detune_amount_ = shaped;
}

void SuperSaw::SetMix(float mix) {
    // 0.0 = center oscillator only, 1.0 = full side oscillator volume
    mix_ = mix;
}

void SuperSaw::SetFilterOffset(float offset) {
    filter_offset_ = offset;
    hpf_.SetFreq(freq_hz_ * filter_offset_, sample_rate_);
}

void SuperSaw::SetAuthentic(bool authentic) {
    authentic_ = authentic;
}

void SuperSaw::Trigger() {
    // Randomize all oscillator phases — matches JP-8000 note-on behavior.
    // Each note press produces slightly different timbral character due
    // to random phase relationships between the 7 oscillators.
    for (int i = 0; i < NUM_OSCS; i++) {
        saw_[i] = Wrap24(static_cast<int32_t>(Rng()));
        saw_f_[i] = static_cast<float>(Rng()) / static_cast<float>(UINT32_MAX) * 2.0f - 1.0f;
    }
    hpf_.Reset();
}

// ============================================================================
// Processing
// ============================================================================

float SuperSaw::Process() {
    float raw = authentic_ ? ProcessAuthentic() : ProcessFloat();

    // Apply pitch-tracked high-pass filter.
    // This removes sub-fundamental aliased harmonics that sound harsh,
    // while preserving the above-fundamental aliasing that adds brightness.
    return hpf_.Process(raw);
}

float SuperSaw::ProcessAuthentic() {
    // ====================================================================
    // AUTHENTIC JP-8000 ALGORITHM — 24-bit fixed-point
    // ====================================================================
    // This matches the reverse-engineered TC170C140 ESP2 firmware.
    // All arithmetic wraps at 24 bits, emulating the original hardware's
    // natural integer overflow behavior.
    // ====================================================================

    int32_t sum = 0;

    for (int i = 0; i < NUM_OSCS; i++) {
        // Calculate per-voice detuning:
        //   voice_detune = (detune_table[i] * pitch_x_detune) >> 7
        //
        // Scaling: full detune (1.0) → ~1 semitone spread on widest pair.
        //   max ratio = 2^(1/12) - 1 ≈ 0.0595
        //   kMaxDetuneScaled = 0.0595 * 128 / 1440 ≈ 0.00529
        static constexpr float kMaxDetuneScaled = 0.00529f;
        int32_t pitch_x_detune = static_cast<int32_t>(
            static_cast<float>(pitch_inc_) * detune_amount_ * kMaxDetuneScaled
        );
        int32_t voice_detune = (static_cast<int64_t>(kDetuneTable[i]) * pitch_x_detune) >> 7;
        voice_detune = Wrap24(voice_detune);

        // Advance phase accumulator.
        // The sawtooth waveform IS the phase value — when the 24-bit
        // integer overflows and wraps, that discontinuity creates the
        // sawtooth edge. No wavetable, no shaping, no anti-aliasing.
        saw_[i] = Wrap24(saw_[i] + pitch_inc_ + voice_detune);

        // Mix: center oscillator at full volume, side oscillators scaled
        if (i == 0) {
            sum = Wrap24(sum + saw_[i]);
        } else {
            // In the original, 'spread' is a 24-bit fixed-point multiply.
            // We approximate with float for the mixing stage since the
            // critical character comes from the oscillator arithmetic.
            int32_t scaled = static_cast<int32_t>(
                static_cast<float>(saw_[i]) * mix_ * mix_
            );
            sum = Wrap24(sum + scaled);
        }
    }

    // Normalize 24-bit sum to float [-1, 1]
    // Scale down to prevent clipping (7 oscillators summed)
    return Int24ToFloat(sum) * 0.3f;
}

float SuperSaw::ProcessFloat() {
    // ====================================================================
    // MODERN FLOATING-POINT MODE
    // ====================================================================
    // Same algorithm structure but in floating-point for comparison.
    // Useful for A/B testing against the authentic mode.
    // ====================================================================

    float sum = 0.0f;
    float phase_inc = freq_hz_ / sample_rate_;

    // Approximate detune ratios derived from the original table.
    // These are normalized: detune_table[i] / 1440 gives relative spread.
    static constexpr float kDetuneRatios[NUM_OSCS] = {
        0.0f,
        128.0f / 1440.0f,    //  0.0889
       -128.0f / 1440.0f,    // -0.0889
        816.0f / 1440.0f,    //  0.5667
       -824.0f / 1440.0f,    // -0.5722
        1408.0f / 1440.0f,   //  0.9778
       -1440.0f / 1440.0f    // -1.0000
    };

    // Max detune in semitones at full detune setting (~1 semitone, matching authentic)
    float max_detune_semitones = 1.0f;
    float detune_factor = detune_amount_ * max_detune_semitones;

    for (int i = 0; i < NUM_OSCS; i++) {
        // Calculate detuned frequency
        float detune_st = kDetuneRatios[i] * detune_factor;
        float detuned_inc = phase_inc * powf(2.0f, detune_st / 12.0f);

        // Advance phase accumulator (wraps at ±1.0)
        saw_f_[i] += detuned_inc * 2.0f;
        if (saw_f_[i] >= 1.0f) saw_f_[i] -= 2.0f;
        if (saw_f_[i] < -1.0f) saw_f_[i] += 2.0f;

        // Mix
        if (i == 0) {
            sum += saw_f_[i];
        } else {
            sum += saw_f_[i] * mix_ * mix_;
        }
    }

    return sum * 0.15f;
}

// ============================================================================
// High-pass filter
// ============================================================================

void SuperSaw::HighPass::SetFreq(float freq_hz, float sr) {
    // One-pole high-pass filter coefficient.
    // Research confirms this is the correct topology: 39C3 reverse engineering,
    // Adam Szabo's analysis, and JE-8086 emulator all use one-pole HPF.
    if (freq_hz < 1.0f) freq_hz = 1.0f;
    if (freq_hz > sr * 0.45f) freq_hz = sr * 0.45f;
    float rc = 1.0f / (2.0f * 3.14159265f * freq_hz);
    float dt = 1.0f / sr;
    coeff = rc / (rc + dt);
}

float SuperSaw::HighPass::Process(float input) {
    // Standard one-pole high-pass: y[n] = α * (y[n-1] + x[n] - x[n-1])
    float output = coeff * (y1 + input - x1);
    x1 = input;
    y1 = output;
    return output;
}

// ============================================================================
// Utilities
// ============================================================================

int32_t SuperSaw::FreqToPhaseInc(float freq_hz) const {
    // Convert frequency to 24-bit phase increment.
    // One full cycle of the sawtooth = 2^24 phase units.
    // phase_inc = freq * 2^24 / sample_rate
    //
    // Example: A440 at 96kHz → 440 * 16777216 / 96000 ≈ 76,891
    return static_cast<int32_t>(freq_hz * static_cast<float>(INT24_RANGE) / sample_rate_);
}

uint32_t SuperSaw::Rng() {
    // Simple xorshift32 PRNG for phase randomization.
    // Does not need to be cryptographic — just reasonably distributed.
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return rng_state_;
}
