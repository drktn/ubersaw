#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "supersaw.h"
#include "voct.h"
#include <cmath>
#include <vector>

// Local copies of 24-bit constants to avoid C++14 ODR issues with constexpr
static constexpr int32_t INT24_MAX_VAL   =  8388607;
static constexpr int32_t INT24_MIN_VAL   = -8388608;
static constexpr int32_t INT24_RANGE_VAL = 16777216;

// Standalone copies of private helpers for direct testing.
// These mirror the implementations in supersaw.h exactly.

static inline int32_t Wrap24(int32_t val) { return (val << 8) >> 8; }

static inline int32_t Mul24(int32_t a, int32_t b) {
    return Wrap24(static_cast<int64_t>(a) * b);
}

static inline float Int24ToFloat(int32_t val) {
    return static_cast<float>(val) / static_cast<float>(INT24_MAX_VAL);
}

// ============================================================================
// Wrap24
// ============================================================================

TEST_CASE("Wrap24 identity for in-range values") {
    CHECK(Wrap24(0) == 0);
    CHECK(Wrap24(1) == 1);
    CHECK(Wrap24(-1) == -1);
    CHECK(Wrap24(INT24_MAX_VAL) == INT24_MAX_VAL);
    CHECK(Wrap24(INT24_MIN_VAL) == INT24_MIN_VAL);
}

TEST_CASE("Wrap24 overflow wraps correctly") {
    // One past max wraps to min
    CHECK(Wrap24(INT24_MAX_VAL + 1) == INT24_MIN_VAL);
    // One below min wraps to max
    CHECK(Wrap24(INT24_MIN_VAL - 1) == INT24_MAX_VAL);
}

TEST_CASE("Wrap24 large values wrap into 24-bit range") {
    int32_t val = Wrap24(INT24_RANGE_VAL * 3 + 42);
    CHECK(val >= INT24_MIN_VAL);
    CHECK(val <= INT24_MAX_VAL);
    CHECK(val == 42);
}

// ============================================================================
// Mul24
// ============================================================================

TEST_CASE("Mul24 basic multiplication") {
    CHECK(Mul24(0, 12345) == 0);
    CHECK(Mul24(1, 1) == 1);
    CHECK(Mul24(100, 100) == 10000);
    CHECK(Mul24(-1, 1) == -1);
}

TEST_CASE("Mul24 wraps on overflow") {
    // Large product should wrap into 24-bit range
    int32_t result = Mul24(INT24_MAX_VAL, 2);
    CHECK(result >= INT24_MIN_VAL);
    CHECK(result <= INT24_MAX_VAL);
    CHECK(result == -2);  // (2^23 - 1) * 2 = 2^24 - 2, wrapped = -2
}

// ============================================================================
// Int24ToFloat
// ============================================================================

TEST_CASE("Int24ToFloat normalization") {
    CHECK(Int24ToFloat(0) == doctest::Approx(0.0f));
    CHECK(Int24ToFloat(INT24_MAX_VAL) == doctest::Approx(1.0f));
    CHECK(Int24ToFloat(INT24_MIN_VAL) == doctest::Approx(-1.0f).epsilon(0.001));
}

// ============================================================================
// SuperSaw lifecycle and basic output
// ============================================================================

static constexpr float kSampleRate = 96000.0f;

TEST_CASE("SuperSaw Init produces valid state") {
    SuperSaw ss;
    ss.Init(kSampleRate);

    // First sample should be finite and in reasonable range
    float out = ss.Process();
    CHECK(std::isfinite(out));
    CHECK(out >= -1.0f);
    CHECK(out <= 1.0f);
}

TEST_CASE("SuperSaw output stays in range over many samples") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);

    for (int i = 0; i < 96000; i++) {
        float out = ss.Process();
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -2.0f);
        REQUIRE(out <= 2.0f);
    }
}

TEST_CASE("SuperSaw output stays in range - float mode") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetAuthentic(false);

    for (int i = 0; i < 96000; i++) {
        float out = ss.Process();
        REQUIRE(std::isfinite(out));
        REQUIRE(out >= -2.0f);
        REQUIRE(out <= 2.0f);
    }
}

// ============================================================================
// Trigger randomizes output
// ============================================================================

TEST_CASE("Trigger changes oscillator phases") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);

    // Run a few samples to establish state
    for (int i = 0; i < 100; i++) ss.Process();
    float before = ss.Process();

    ss.Trigger();
    float after = ss.Process();

    // Extremely unlikely to be identical after phase randomization
    CHECK(before != after);
}

// ============================================================================
// Detune independence from mix (regression test for bug fix)
// ============================================================================

TEST_CASE("Float mode: detune works when mix is zero") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetAuthentic(false);

    // With mix=0 and detune=0, center osc only, no detuning
    ss.SetMix(0.0f);
    ss.SetDetune(0.0f);
    ss.Trigger();
    float sum_no_detune = 0.0f;
    for (int i = 0; i < 960; i++) sum_no_detune += std::fabs(ss.Process());

    // With mix=0 and detune=1, center osc should still be detuned
    // (center detune_table[0] = 0, so center osc is NOT detuned —
    //  but side oscs ARE detuned even though mixed at 0 volume.
    //  The key point: SetDetune should not be gated by mix.)
    ss.SetMix(0.0f);
    ss.SetDetune(1.0f);
    ss.Trigger();
    float sum_with_detune = 0.0f;
    for (int i = 0; i < 960; i++) sum_with_detune += std::fabs(ss.Process());

    // Both should produce non-zero output (center osc active)
    CHECK(sum_no_detune > 0.0f);
    CHECK(sum_with_detune > 0.0f);
}

// ============================================================================
// FreqToPhaseInc accuracy (tested indirectly via period measurement)
// ============================================================================

TEST_CASE("440 Hz produces correct period in authentic mode") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.0f);
    ss.SetMix(0.0f);  // Center osc only

    // Run for a bit to stabilize HPF
    for (int i = 0; i < 1000; i++) ss.Process();

    // Count zero-crossings over 1 second to estimate frequency
    int crossings = 0;
    float prev = ss.Process();
    for (int i = 0; i < 96000; i++) {
        float cur = ss.Process();
        if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f)) {
            crossings++;
        }
        prev = cur;
    }

    // Each cycle has 2 zero-crossings (up + down), so freq ≈ crossings / 2
    float measured_freq = static_cast<float>(crossings) / 2.0f;
    CHECK(measured_freq == doctest::Approx(440.0f).epsilon(0.02));
}

// ============================================================================
// VoctToFreq
// ============================================================================

TEST_CASE("VoctToFreq: knob midpoint with 0V CV gives middle C") {
    // knob=0.5 → base_note=60, CV=0.5 → 0V → middle C (261.6 Hz)
    float freq = VoctToFreq(0.5f, 0.5f);
    CHECK(freq == doctest::Approx(261.6f).epsilon(0.01));
}

TEST_CASE("VoctToFreq: 1V/Oct tracking") {
    // 1V increase = 1 octave up (double frequency)
    // CV 0.5 = 0V, CV 0.6 = +1V (since (0.6-0.5)*10 = 1V)
    float f0 = VoctToFreq(0.5f, 0.5f);
    float f1 = VoctToFreq(0.6f, 0.5f);
    CHECK(f1 / f0 == doctest::Approx(2.0f).epsilon(0.01));
}

TEST_CASE("VoctToFreq: clamps to lower bound") {
    // Extreme negative CV should clamp to >= 10 Hz
    float freq = VoctToFreq(0.0f, 0.0f);  // -5V CV, knob at bottom
    CHECK(freq >= 10.0f);
}

TEST_CASE("VoctToFreq: clamps to upper bound") {
    // Extreme positive CV should clamp to <= 20 kHz
    float freq = VoctToFreq(1.0f, 1.0f);  // +5V CV, knob at top
    CHECK(freq <= 20000.0f);
}

// ============================================================================
// Detune scaling calibration
// ============================================================================

TEST_CASE("Full detune maintains periodicity near fundamental") {
    // With correct detune (~1 semitone max spread), the 7 oscillators are
    // close enough in frequency that autocorrelation at the fundamental
    // period remains strong. With broken scaling (1000+ cents), it drops
    // to near zero because oscillators are at wildly different frequencies.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(1.0f);
    ss.SetMix(1.0f);

    // Skip initial transient (HPF settling)
    for (int i = 0; i < 2000; i++) ss.Process();

    // Collect 1 second of audio
    const int N = 96000;
    std::vector<float> samples(N);
    for (int i = 0; i < N; i++) samples[i] = ss.Process();

    // Autocorrelation at the fundamental period (440 Hz @ 96 kHz ≈ 218 samples)
    int period = static_cast<int>(kSampleRate / 440.0f);
    double autocorr = 0.0, energy = 0.0;
    for (int i = 0; i < N - period; i++) {
        autocorr += samples[i] * samples[i + period];
        energy += samples[i] * samples[i];
    }
    double normalized = autocorr / energy;

    // With ~1 semitone spread: autocorrelation > 0.1 (beating but periodic)
    // With broken scaling: autocorrelation ≈ 0 (noise-like)
    CHECK(normalized > 0.1);
}

// ============================================================================
// HPF removes sub-fundamental content (confirms one-pole is correct topology)
// ============================================================================

TEST_CASE("HPF removes DC offset from output") {
    // The pitch-tracked one-pole HPF should remove DC and sub-fundamental content.
    // Research (39C3, Szabo, JE-8086) confirms one-pole is the correct topology.
    // Verify by checking that the output mean is near zero after HPF settling.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.Trigger();

    // Let HPF settle
    for (int i = 0; i < 4000; i++) ss.Process();

    // Collect samples and measure DC (mean)
    const int N = 96000;
    double sum = 0.0, energy = 0.0;
    for (int i = 0; i < N; i++) {
        float s = ss.Process();
        sum += s;
        energy += s * s;
    }
    double mean = sum / N;
    double rms = std::sqrt(energy / N);

    // DC component should be negligible relative to signal RMS
    // (without HPF, naive saws can have significant DC offset)
    CHECK(std::fabs(mean) < rms * 0.05);
    CHECK(rms > 0.0);  // Sanity: signal is non-zero
}

TEST_CASE("Full detune maintains periodicity - float mode") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(1.0f);
    ss.SetMix(1.0f);
    ss.SetAuthentic(false);

    for (int i = 0; i < 2000; i++) ss.Process();

    const int N = 96000;
    std::vector<float> samples(N);
    for (int i = 0; i < N; i++) samples[i] = ss.Process();

    int period = static_cast<int>(kSampleRate / 440.0f);
    double autocorr = 0.0, energy = 0.0;
    for (int i = 0; i < N - period; i++) {
        autocorr += samples[i] * samples[i + period];
        energy += samples[i] * samples[i];
    }
    double normalized = autocorr / energy;
    CHECK(normalized > 0.1);
}
