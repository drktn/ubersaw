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
    // Scale the 0..1 user parameter to a fixed-point value.
    // The exact scaling determines how wide the detune spreads.
    // On the JP-8000, the detune knob maps through MIDI CC (0-127).
    // We scale to a range that produces musically useful detuning.
    //
    // NOTE: The precise mapping from the JP-8000's front panel to the
    // internal 'detune' variable involves the synth's parameter scaling,
    // which may need tuning by ear against the original hardware.
    detune_param_ = static_cast<int32_t>(detune * 127.0f);
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
        //   voice_detune = (detune_table[i] * (pitch * detune)) >> 7
        //
        // The multiplication of pitch × detune scales the detune offset
        // proportionally to frequency — higher notes get wider absolute
        // detuning, maintaining consistent musical intervals.
        int32_t pitch_x_detune = Mul24(pitch_inc_, detune_param_);
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
                static_cast<float>(saw_[i]) * mix_
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

    // Max detune in semitones at full detune setting (approximate)
    float max_detune_semitones = 0.5f;
    float detune_factor = (mix_ > 0.0f) ?
        (static_cast<float>(detune_param_) / 127.0f) * max_detune_semitones : 0.0f;

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
            sum += saw_f_[i] * mix_;
        }
    }

    return sum * 0.15f;
}

// ============================================================================
// High-pass filter
// ============================================================================

void SuperSaw::HighPass::SetFreq(float freq_hz, float sr) {
    // One-pole high-pass filter coefficient.
    // The JP-8000 likely uses something more sophisticated (possibly a
    // multi-pole SVF), but a one-pole HPF captures the essential behavior
    // of removing sub-fundamental content while tracking pitch.
    //
    // TODO: Investigate 24dB SVF HPF as suggested by community analysis
    // for closer match to original filter characteristics.
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
