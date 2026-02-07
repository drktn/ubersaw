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

    // Initialize glide (off by default)
    glide_time_ = 0.0f;
    glide_coeff_ = 0.0f;
    current_freq_ = 440.0f;
    target_freq_ = 440.0f;

    // Initialize anti-click parameter smoothers
    static constexpr float kParamSmooth = 0.99f;
    smooth_detune_.Init(kParamSmooth);
    smooth_mix_.Init(kParamSmooth);
    smooth_filter_offset_.Init(kParamSmooth);
    smooth_spread_.Init(kParamSmooth);

    // Default parameters
    SetFreq(440.0f);
    SetDetune(0.5f);
    SetMix(1.0f);
    SetFilterOffset(1.0f);
    SetSpread(0.0f);

    // Force smoothers to initial values (no ramp on startup)
    smooth_detune_.SetValue(target_detune_);
    smooth_mix_.SetValue(target_mix_);
    smooth_filter_offset_.SetValue(target_filter_offset_);
    smooth_spread_.SetValue(target_spread_);

    // Initialize HPFs
    hpf_.Reset();
    hpf_.SetFreq(freq_hz_ * filter_offset_, sample_rate_);
    hpf_r_.Reset();
    hpf_r_.SetFreq(freq_hz_ * filter_offset_, sample_rate_);
}

// ============================================================================
// Parameter setters
// ============================================================================

void SuperSaw::SetFreq(float freq_hz) {
    target_freq_ = freq_hz;

    if (glide_time_ <= 0.0f) {
        // No glide: snap instantly (preserves existing behavior)
        current_freq_ = freq_hz;
    }
    // When gliding, current_freq_ moves toward target_freq_ each sample in Process()

    freq_hz_ = current_freq_;
    pitch_inc_ = FreqToPhaseInc(current_freq_);
    hpf_.SetFreq(freq_hz_ * filter_offset_, sample_rate_);
}

void SuperSaw::SetGlide(float time_sec) {
    glide_time_ = time_sec;
    if (time_sec > 0.0f) {
        // Exponential smoothing: reach ~99.3% of target in glide_time.
        // coeff = 1 - exp(-5 / (time * sr))
        // Factor of 5 gives ~99.3% convergence (5 time constants).
        glide_coeff_ = 1.0f - expf(-5.0f / (time_sec * sample_rate_));
    } else {
        glide_coeff_ = 0.0f;
    }
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

    target_detune_ = shaped;
}

void SuperSaw::SetMix(float mix) {
    // 0.0 = center oscillator only, 1.0 = full side oscillator volume
    target_mix_ = mix;
}

void SuperSaw::SetFilterOffset(float offset) {
    target_filter_offset_ = offset;
}

void SuperSaw::SetAuthentic(bool authentic) {
    authentic_ = authentic;
}

void SuperSaw::SetSpread(float spread) {
    target_spread_ = spread;
}

void SuperSaw::SetVoiceCount(int count) {
    // Snap to nearest valid: 1, 3, 5, 7
    if (count <= 2)      voice_count_ = 1;
    else if (count <= 4) voice_count_ = 3;
    else if (count <= 6) voice_count_ = 5;
    else                 voice_count_ = 7;
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

void SuperSaw::UpdateParams() {
    // Update glide: move current_freq_ toward target_freq_
    if (glide_time_ > 0.0f && current_freq_ != target_freq_) {
        current_freq_ += (target_freq_ - current_freq_) * glide_coeff_;
        // Snap when close enough to avoid infinite asymptote
        if (std::fabs(current_freq_ - target_freq_) < 0.01f)
            current_freq_ = target_freq_;
        freq_hz_ = current_freq_;
        pitch_inc_ = FreqToPhaseInc(current_freq_);
    }

    // Anti-click: smooth detune, mix, filter_offset, and spread per sample
    detune_amount_ = smooth_detune_.Process(target_detune_);
    mix_ = smooth_mix_.Process(target_mix_);
    filter_offset_ = smooth_filter_offset_.Process(target_filter_offset_);
    spread_ = smooth_spread_.Process(target_spread_);
    float hpf_freq = freq_hz_ * filter_offset_;
    hpf_.SetFreq(hpf_freq, sample_rate_);
    hpf_r_.SetFreq(hpf_freq, sample_rate_);
}

float SuperSaw::Process() {
    UpdateParams();

    float raw = authentic_ ? ProcessAuthentic() : ProcessFloat();

    // Apply pitch-tracked high-pass filter.
    // This removes sub-fundamental aliased harmonics that sound harsh,
    // while preserving the above-fundamental aliasing that adds brightness.
    return hpf_.Process(raw);
}

void SuperSaw::ProcessStereo(float& left, float& right) {
    UpdateParams();

    if (authentic_) {
        ProcessAuthenticStereo(left, right);
    } else {
        ProcessFloatStereo(left, right);
    }

    left = hpf_.Process(left);
    right = hpf_r_.Process(right);
}

void SuperSaw::ProcessBlock(float* out, size_t n) {
    UpdateParams();
    for (size_t i = 0; i < n; i++) {
        float raw = authentic_ ? ProcessAuthentic() : ProcessFloat();
        out[i] = hpf_.Process(raw);
    }
}

void SuperSaw::ProcessBlockStereo(float* left, float* right, size_t n) {
    UpdateParams();
    for (size_t i = 0; i < n; i++) {
        float l, r;
        if (authentic_) {
            ProcessAuthenticStereo(l, r);
        } else {
            ProcessFloatStereo(l, r);
        }
        left[i] = hpf_.Process(l);
        right[i] = hpf_r_.Process(r);
    }
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

    for (int i = 0; i < voice_count_; i++) {
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

    for (int i = 0; i < voice_count_; i++) {
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

void SuperSaw::ProcessAuthenticStereo(float& left, float& right) {
    // Same oscillator advancement as ProcessAuthentic, but accumulates
    // into separate L/R sums based on detune direction and spread.
    // Positive-detune oscs (i=1,3,5) pan right, negative (i=2,4,6) pan left.
    // Center osc (i=0) is equal in both channels.

    int32_t sum_l = 0;
    int32_t sum_r = 0;

    for (int i = 0; i < voice_count_; i++) {
        static constexpr float kMaxDetuneScaled = 0.00529f;
        int32_t pitch_x_detune = static_cast<int32_t>(
            static_cast<float>(pitch_inc_) * detune_amount_ * kMaxDetuneScaled
        );
        int32_t voice_detune = (static_cast<int64_t>(kDetuneTable[i]) * pitch_x_detune) >> 7;
        voice_detune = Wrap24(voice_detune);

        saw_[i] = Wrap24(saw_[i] + pitch_inc_ + voice_detune);

        if (i == 0) {
            // Center osc: equal in both channels
            sum_l = Wrap24(sum_l + saw_[i]);
            sum_r = Wrap24(sum_r + saw_[i]);
        } else {
            int32_t scaled = static_cast<int32_t>(
                static_cast<float>(saw_[i]) * mix_ * mix_
            );
            // Pan based on detune direction:
            // Odd indices (1,3,5) have positive detune -> pan right
            // Even indices (2,4,6) have negative detune -> pan left
            float pan_r = 0.5f + (i % 2 == 1 ? spread_ * 0.5f : -spread_ * 0.5f);
            float pan_l = 1.0f - pan_r;
            sum_l = Wrap24(sum_l + static_cast<int32_t>(static_cast<float>(scaled) * pan_l * 2.0f));
            sum_r = Wrap24(sum_r + static_cast<int32_t>(static_cast<float>(scaled) * pan_r * 2.0f));
        }
    }

    left  = Int24ToFloat(sum_l) * 0.3f;
    right = Int24ToFloat(sum_r) * 0.3f;
}

void SuperSaw::ProcessFloatStereo(float& left, float& right) {
    // Same oscillator advancement as ProcessFloat, but accumulates
    // into separate L/R sums based on detune direction and spread.

    float sum_l = 0.0f;
    float sum_r = 0.0f;
    float phase_inc = freq_hz_ / sample_rate_;

    static constexpr float kDetuneRatios[NUM_OSCS] = {
        0.0f,
        128.0f / 1440.0f,
       -128.0f / 1440.0f,
        816.0f / 1440.0f,
       -824.0f / 1440.0f,
        1408.0f / 1440.0f,
       -1440.0f / 1440.0f
    };

    float max_detune_semitones = 1.0f;
    float detune_factor = detune_amount_ * max_detune_semitones;

    for (int i = 0; i < voice_count_; i++) {
        float detune_st = kDetuneRatios[i] * detune_factor;
        float detuned_inc = phase_inc * powf(2.0f, detune_st / 12.0f);

        saw_f_[i] += detuned_inc * 2.0f;
        if (saw_f_[i] >= 1.0f) saw_f_[i] -= 2.0f;
        if (saw_f_[i] < -1.0f) saw_f_[i] += 2.0f;

        if (i == 0) {
            sum_l += saw_f_[i];
            sum_r += saw_f_[i];
        } else {
            float val = saw_f_[i] * mix_ * mix_;
            float pan_r = 0.5f + (i % 2 == 1 ? spread_ * 0.5f : -spread_ * 0.5f);
            float pan_l = 1.0f - pan_r;
            sum_l += val * pan_l * 2.0f;
            sum_r += val * pan_r * 2.0f;
        }
    }

    left  = sum_l * 0.15f;
    right = sum_r * 0.15f;
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
