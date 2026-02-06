// ============================================================================
// ÜBERSAW — JP-8000 Supersaw Engine
// ============================================================================
// Faithful recreation of the Roland JP-8000 supersaw algorithm as
// reverse-engineered from the TC170C140 ESP2 DSP chip, presented at 39C3.
//
// Architecture:
//   - 7 naive sawtooth phase accumulators (no band-limiting)
//   - 24-bit fixed-point integer arithmetic with natural overflow wrapping
//   - Asymmetric detune table: {0, 128, -128, 816, -824, 1408, -1440}
//   - Pitch-tracked high-pass filter on output
//   - Random phase initialization on note trigger
//
// Reference: "From Silicon to Darude Sand-storm" — Giulioz, 39C3 (2025)
// ============================================================================

#pragma once

#include <cstdint>

class SuperSaw {
public:
    // Number of oscillators in the JP-8000 supersaw
    static constexpr int NUM_OSCS = 7;

    // 24-bit integer range constants
    static constexpr int32_t INT24_MAX =  8388607;   // (2^23) - 1
    static constexpr int32_t INT24_MIN = -8388608;    // -(2^23)
    static constexpr int32_t INT24_RANGE = 16777216;  // 2^24

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Initialize the engine. Call once before processing.
    /// @param sample_rate  Audio sample rate in Hz (e.g. 96000.0f)
    void Init(float sample_rate);

    /// Process one audio sample. Call once per sample in the audio callback.
    /// @return  The supersaw output sample, normalized to approximately [-1, 1]
    float Process();

    // -----------------------------------------------------------------------
    // Parameter setters — call from audio callback after reading controls
    // -----------------------------------------------------------------------

    /// Set base frequency in Hz (from V/Oct CV + coarse knob).
    void SetFreq(float freq_hz);

    /// Set detune amount. Range: 0.0 (unison) to 1.0 (full JP-8000 spread).
    void SetDetune(float detune);

    /// Set mix/spread between center and side oscillators.
    /// 0.0 = center only, 1.0 = full side oscillator mix.
    void SetMix(float mix);

    /// Set high-pass filter cutoff as an offset ratio from the fundamental.
    /// 1.0 = track fundamental exactly. Higher = offset above fundamental.
    void SetFilterOffset(float offset);

    /// Trigger a new note — randomizes all oscillator phases.
    void Trigger();

    /// Set whether to use authentic 24-bit fixed-point mode (true) or
    /// floating-point mode (false). Default: true.
    void SetAuthentic(bool authentic);

private:
    // -----------------------------------------------------------------------
    // 24-bit fixed-point helpers
    // -----------------------------------------------------------------------

    /// Wrap a 32-bit value to signed 24-bit range [-8388608, 8388607].
    /// This emulates the natural integer overflow of the TC170C140 ESP2.
    static inline int32_t Wrap24(int32_t val) {
        // Shift bit 23 into bit 31 (sign position), then arithmetic right
        // shift back — this sign-extends from 24 bits in a single operation.
        return (val << 8) >> 8;
    }

    /// Fixed-point multiply: (a * b) wrapped to 24 bits.
    /// Uses 64-bit intermediate to avoid 32-bit overflow before wrapping.
    static inline int32_t Mul24(int32_t a, int32_t b) {
        return Wrap24(static_cast<int64_t>(a) * b);
    }

    /// Convert a 24-bit signed integer to a normalized float [-1, 1].
    static inline float Int24ToFloat(int32_t val) {
        return static_cast<float>(val) / static_cast<float>(INT24_MAX);
    }

    /// Convert a frequency in Hz to a 24-bit phase increment per sample.
    int32_t FreqToPhaseInc(float freq_hz) const;

    // -----------------------------------------------------------------------
    // Simple one-pole high-pass filter (pitch-tracked)
    // -----------------------------------------------------------------------

    struct HighPass {
        float y1 = 0.0f;  // Previous output
        float x1 = 0.0f;  // Previous input

        void Reset() { y1 = 0.0f; x1 = 0.0f; }

        /// Set cutoff frequency. Computes coefficient from sample rate.
        void SetFreq(float freq_hz, float sr);

        /// Process one sample, returns high-pass filtered output.
        float Process(float input);

        float coeff = 0.999f;  // Filter coefficient (close to 1 = low cutoff)
    };

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    float sample_rate_ = 96000.0f;

    // The original JP-8000 detune table — asymmetric by design
    static constexpr int32_t kDetuneTable[NUM_OSCS] = {
        0, 128, -128, 816, -824, 1408, -1440
    };

    // Oscillator phase accumulators (24-bit fixed-point)
    int32_t saw_[NUM_OSCS] = {};

    // Parameters (in engine-internal formats)
    int32_t pitch_inc_ = 0;       // Base pitch as 24-bit phase increment
    int32_t detune_param_ = 0;    // Detune amount (scaled for fixed-point math)
    float   mix_ = 1.0f;          // Side oscillator mix (0..1)
    float   freq_hz_ = 440.0f;    // Current frequency for filter tracking
    float   filter_offset_ = 1.0f;// HPF cutoff offset ratio
    bool    authentic_ = true;     // True = 24-bit mode, false = float mode

    // High-pass filter
    HighPass hpf_;

    // Random number state (for phase randomization)
    uint32_t rng_state_ = 0x12345678;
    uint32_t Rng();

    // -----------------------------------------------------------------------
    // Internal processing paths
    // -----------------------------------------------------------------------

    /// Authentic 24-bit fixed-point processing (matches original hardware)
    float ProcessAuthentic();

    /// Floating-point processing (modern, clean alternative)
    float ProcessFloat();

    // Floating-point oscillator state (used in float mode)
    float saw_f_[NUM_OSCS] = {};
};
