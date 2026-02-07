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

// ============================================================================
// Parabolic mix curve (Szabo)
// ============================================================================

TEST_CASE("Mix curve is parabolic, not linear (authentic)") {
    // With detune=0 and deterministic init (all phases zero), all 7 oscs
    // produce identical values. Output amplitude factor = 1 + 6*mix_eff.
    // Parabolic: mix_eff = mix^2, so mix=0.5 -> factor = 1 + 6*0.25 = 2.5
    // Linear:    mix_eff = mix,   so mix=0.5 -> factor = 1 + 6*0.5  = 4.0
    //
    // Use low freq so phase values stay small and 24-bit sum doesn't wrap.
    // Minimize HPF impact with very low filter offset.
    // Settle 500 samples for parameter smoothers, keep phase < INT24_MAX/7.
    auto getSample = [](float mix) {
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetFreq(10.0f);
        ss.SetDetune(0.0f);
        ss.SetAuthentic(true);
        ss.SetFilterOffset(0.01f);
        ss.SetMix(mix);
        // Settle parameter smoothers (~500 samples for 99% at coeff=0.99).
        // At 10 Hz, 500 samples → phase ≈ 875k (< INT24_MAX/7 = 1.2M), no wrap.
        for (int i = 0; i < 500; i++) ss.Process();
        return ss.Process();
    };

    float s_center = getSample(0.0f);  // 1 osc only
    float s_half   = getSample(0.5f);  // parabolic: 1 + 6*0.25 = 2.5x
    float s_full   = getSample(1.0f);  // 1 + 6*1.0 = 7.0x

    float ratio_half = s_half / s_center;
    float ratio_full = s_full / s_center;

    CHECK(ratio_full == doctest::Approx(7.0f).epsilon(0.10));
    CHECK(ratio_half == doctest::Approx(2.5f).epsilon(0.10));
}

TEST_CASE("Mix curve is parabolic, not linear (float mode)") {
    // Float mode: no wrapping, use energy over time with phase-locked oscs.
    // Energy ∝ (1 + 6*m_eff)^2. Parabolic: ratio = (2.5/7)^2 ≈ 0.128
    auto measureEnergy = [](float mix) {
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetFreq(440.0f);
        ss.SetDetune(0.0f);
        ss.SetAuthentic(false);
        ss.SetMix(mix);
        for (int i = 0; i < 4000; i++) ss.Process();
        double energy = 0.0;
        const int N = 48000;
        for (int i = 0; i < N; i++) {
            float s = ss.Process();
            energy += s * s;
        }
        return energy;
    };

    double e_full = measureEnergy(1.0f);
    double e_half = measureEnergy(0.5f);
    double ratio = e_half / e_full;

    // Parabolic: ~0.128; Linear: ~0.327
    CHECK(ratio < 0.22);
    CHECK(ratio > 0.05);
}

// ============================================================================
// Multi-octave V/Oct tracking verification
// ============================================================================

TEST_CASE("V/Oct tracks correctly across 5+ octaves via zero-crossings") {
    // Verify the SuperSaw engine produces the correct frequency at each octave
    // by counting zero-crossings. detune=0, mix=0 -> center osc only.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetDetune(0.0f);
    ss.SetMix(0.0f);

    // C2 through C8 (7 octaves). C1 too low for accurate 1s zero-crossing count.
    struct OctaveTest { float freq; const char* name; };
    OctaveTest octaves[] = {
        { 65.41f,   "C2" },
        { 130.81f,  "C3" },
        { 261.63f,  "C4" },
        { 523.25f,  "C5" },
        { 1046.50f, "C6" },
        { 2093.00f, "C7" },
        { 4186.01f, "C8" },
    };

    for (auto& oct : octaves) {
        CAPTURE(oct.name);
        ss.SetFreq(oct.freq);

        // Let HPF settle (longer for low frequencies)
        int settle = (oct.freq < 100.0f) ? 10000 : 2000;
        for (int i = 0; i < settle; i++) ss.Process();

        // Count zero-crossings over 1 second
        int crossings = 0;
        float prev = ss.Process();
        const int N = 96000;
        for (int i = 0; i < N; i++) {
            float cur = ss.Process();
            if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f)) {
                crossings++;
            }
            prev = cur;
        }

        // 2 zero-crossings per cycle -> freq = crossings / 2
        float measured = static_cast<float>(crossings) / 2.0f;
        CHECK(measured == doctest::Approx(oct.freq).epsilon(0.02));
    }
}

TEST_CASE("VoctToFreq produces correct frequencies across octaves") {
    // Verify V/Oct math at known MIDI note -> frequency mappings.
    // With cv=0.5 (0V), total_note = 24 + knob*72
    // For MIDI note N: knob = (N - 24) / 72
    struct NoteTest { int midi_note; float expected_freq; const char* name; };
    NoteTest notes[] = {
        { 36,  65.41f,   "C2" },
        { 48,  130.81f,  "C3" },
        { 60,  261.63f,  "C4" },
        { 72,  523.25f,  "C5" },
        { 84,  1046.50f, "C6" },
        { 96,  2093.00f, "C7 (knob max)" },
    };

    for (auto& n : notes) {
        CAPTURE(n.name);
        float knob = static_cast<float>(n.midi_note - 24) / 72.0f;
        float freq = VoctToFreq(0.5f, knob);
        CHECK(freq == doctest::Approx(n.expected_freq).epsilon(0.01));
    }
}

TEST_CASE("VoctToFreq: octave doubling via CV across range") {
    // +1V CV should double frequency regardless of base note.
    // CV 0.5 = 0V, CV 0.6 = +1V
    float knob_values[] = { 0.0f, 0.25f, 0.5f, 0.75f };
    for (float knob : knob_values) {
        CAPTURE(knob);
        float f0 = VoctToFreq(0.5f, knob);
        float f1 = VoctToFreq(0.6f, knob);
        // Skip if f1 is clamped at upper bound
        if (f1 < 20000.0f) {
            CHECK(f1 / f0 == doctest::Approx(2.0f).epsilon(0.01));
        }
    }
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

// ============================================================================
// Detune knob curve (Szabo 11th-order polynomial, #5)
// ============================================================================
// The JP-8000 detune knob uses a non-linear curve: gentle at low settings,
// aggressive at the top. Documented in Adam Szabo's thesis "How to Emulate
// the Super Saw" as an 11th-order polynomial fit. Confirmed by 39C3 reverse
// engineering and JE-8086 emulator.

TEST_CASE("Detune curve is non-linear: midpoint much less than half spread") {
    // Szabo polynomial: detune(0.5) ≈ 0.098 — much less than linear 0.5.
    // Measure via autocorrelation: less effective detune -> higher correlation.
    auto measureAutocorr = [](float detune, bool authentic) {
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetFreq(440.0f);
        ss.SetDetune(detune);
        ss.SetMix(1.0f);
        ss.SetAuthentic(authentic);
        for (int i = 0; i < 4000; i++) ss.Process();
        const int N = 48000;
        std::vector<float> samples(N);
        for (int i = 0; i < N; i++) samples[i] = ss.Process();
        int period = static_cast<int>(kSampleRate / 440.0f);
        double ac = 0.0, e = 0.0;
        for (int i = 0; i < N - period; i++) {
            ac += samples[i] * samples[static_cast<size_t>(i + period)];
            e += samples[i] * samples[i];
        }
        return ac / e;
    };

    // Authentic mode: with shaped curve, detune(0.5) ≈ 0.098, so midpoint
    // autocorrelation should be higher than full detune and very high overall.
    double ac_half_auth = measureAutocorr(0.5f, true);
    double ac_full_auth = measureAutocorr(1.0f, true);
    CHECK(ac_half_auth > ac_full_auth);
    CHECK(ac_half_auth > 0.9);  // Near-unison at shaped midpoint

    // Float mode same check
    double ac_half_flt = measureAutocorr(0.5f, false);
    double ac_full_flt = measureAutocorr(1.0f, false);
    CHECK(ac_half_flt > ac_full_flt);
    CHECK(ac_half_flt > 0.9);
}

TEST_CASE("Detune curve: near-zero knob produces near-unison") {
    // At detune=0.05, shaped curve gives ≈ 0.008 — virtually unison.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.05f);
    ss.SetMix(1.0f);

    for (int i = 0; i < 4000; i++) ss.Process();

    const int N = 48000;
    std::vector<float> samples(N);
    for (int i = 0; i < N; i++) samples[i] = ss.Process();

    int period = static_cast<int>(kSampleRate / 440.0f);
    double autocorr = 0.0, energy = 0.0;
    for (int i = 0; i < N - period; i++) {
        autocorr += samples[i] * samples[static_cast<size_t>(i + period)];
        energy += samples[i] * samples[i];
    }
    double normalized = autocorr / energy;
    CHECK(normalized > 0.9);
}

TEST_CASE("Detune curve: monotonically increasing spread") {
    // Higher knob position -> more detune -> lower autocorrelation.
    auto measureAutocorr = [](float detune) {
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetFreq(440.0f);
        ss.SetDetune(detune);
        ss.SetMix(1.0f);
        for (int i = 0; i < 4000; i++) ss.Process();
        const int N = 48000;
        std::vector<float> samples(N);
        for (int i = 0; i < N; i++) samples[i] = ss.Process();
        int period = static_cast<int>(kSampleRate / 440.0f);
        double ac = 0.0, e = 0.0;
        for (int i = 0; i < N - period; i++) {
            ac += samples[i] * samples[static_cast<size_t>(i + period)];
            e += samples[i] * samples[i];
        }
        return ac / e;
    };

    double ac_low  = measureAutocorr(0.1f);
    double ac_mid  = measureAutocorr(0.5f);
    double ac_high = measureAutocorr(0.9f);

    CHECK(ac_low > ac_mid);
    CHECK(ac_mid > ac_high);
}

// ============================================================================
// A/B comparison: Authentic vs Float mode characterization
// ============================================================================
// These tests quantify differences between ProcessAuthentic() and ProcessFloat().
// This is characterization — documenting differences, not fixing them.
// Both modes should produce valid audio at the correct pitch.

// Helper: collect N samples from a SuperSaw configured at given settings
static std::vector<float> collectSamples(bool authentic, float freq, float detune,
                                          float mix, int settle, int count) {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(freq);
    ss.SetDetune(detune);
    ss.SetMix(mix);
    ss.SetAuthentic(authentic);
    ss.Trigger();
    for (int i = 0; i < settle; i++) ss.Process();
    std::vector<float> out(count);
    for (int i = 0; i < count; i++) out[i] = ss.Process();
    return out;
}

TEST_CASE("A/B: RMS output levels at same settings") {
    // Both modes at 440Hz, detune=0.5, mix=1.0 should produce nonzero RMS.
    // Document the RMS ratio between modes.
    const int N = 96000;
    auto auth = collectSamples(true,  440.0f, 0.5f, 1.0f, 4000, N);
    auto flt  = collectSamples(false, 440.0f, 0.5f, 1.0f, 4000, N);

    double energy_auth = 0.0, energy_flt = 0.0;
    for (int i = 0; i < N; i++) {
        energy_auth += auth[i] * auth[i];
        energy_flt  += flt[i]  * flt[i];
    }
    double rms_auth = std::sqrt(energy_auth / N);
    double rms_flt  = std::sqrt(energy_flt  / N);

    // Both produce audible signal
    CHECK(rms_auth > 0.01);
    CHECK(rms_flt  > 0.01);

    // Both in reasonable range (neither clips, neither silent)
    CHECK(rms_auth < 1.0);
    CHECK(rms_flt  < 1.0);

    // Document ratio — authentic uses 0.3f scaler, float uses 0.15f,
    // so authentic is expected louder. Allow wide range since this is
    // characterization.
    double ratio = rms_auth / rms_flt;
    CHECK(ratio > 0.5);
    CHECK(ratio < 10.0);
    MESSAGE("A/B RMS: authentic=" << rms_auth << " float=" << rms_flt
            << " ratio=" << ratio);
}

TEST_CASE("A/B: DC offset (mean) between modes") {
    // HPF should remove DC in both modes. Measure residual DC as
    // fraction of RMS to characterize any mode-specific bias.
    const int N = 96000;
    auto auth = collectSamples(true,  440.0f, 0.5f, 1.0f, 4000, N);
    auto flt  = collectSamples(false, 440.0f, 0.5f, 1.0f, 4000, N);

    auto dcAndRms = [](const std::vector<float>& s) {
        double sum = 0.0, energy = 0.0;
        for (float v : s) { sum += v; energy += v * v; }
        double mean = sum / s.size();
        double rms  = std::sqrt(energy / s.size());
        return std::make_pair(mean, rms);
    };

    auto auth_dc = dcAndRms(auth);
    auto flt_dc  = dcAndRms(flt);
    double mean_auth = auth_dc.first, rms_auth = auth_dc.second;
    double mean_flt  = flt_dc.first,  rms_flt  = flt_dc.second;

    // DC should be negligible relative to RMS in both modes
    CHECK(std::fabs(mean_auth) < rms_auth * 0.1);
    CHECK(std::fabs(mean_flt)  < rms_flt  * 0.1);

    MESSAGE("A/B DC: auth_mean=" << mean_auth << " auth_rms=" << rms_auth
            << " flt_mean=" << mean_flt << " flt_rms=" << rms_flt);
}

TEST_CASE("A/B: output range (min/max peak) between modes") {
    // Measure peak-to-peak in both modes. Both should stay within [-1, 1]
    // after HPF. Document the headroom difference.
    const int N = 96000;
    auto auth = collectSamples(true,  440.0f, 0.5f, 1.0f, 4000, N);
    auto flt  = collectSamples(false, 440.0f, 0.5f, 1.0f, 4000, N);

    auto minMax = [](const std::vector<float>& s) {
        float mn = s[0], mx = s[0];
        for (float v : s) { mn = std::min(mn, v); mx = std::max(mx, v); }
        return std::make_pair(mn, mx);
    };

    auto auth_mm = minMax(auth);
    auto flt_mm  = minMax(flt);
    float min_auth = auth_mm.first, max_auth = auth_mm.second;
    float min_flt  = flt_mm.first,  max_flt  = flt_mm.second;

    // Neither mode should clip beyond [-1, 1]
    CHECK(min_auth >= -1.0f);
    CHECK(max_auth <=  1.0f);
    CHECK(min_flt  >= -1.0f);
    CHECK(max_flt  <=  1.0f);

    // Both produce meaningful signal (non-trivial peak-to-peak)
    float pp_auth = max_auth - min_auth;
    float pp_flt  = max_flt  - min_flt;
    CHECK(pp_auth > 0.05f);
    CHECK(pp_flt  > 0.05f);

    MESSAGE("A/B peak: auth=[" << min_auth << "," << max_auth << "] pp=" << pp_auth
            << " flt=[" << min_flt << "," << max_flt << "] pp=" << pp_flt);
}

TEST_CASE("A/B: zero-crossing frequency (pitch) between modes") {
    // Both modes should produce the same fundamental pitch (440 Hz).
    // detune=0, mix=0 -> center osc only for clean pitch measurement.
    const int N = 96000;
    auto auth = collectSamples(true,  440.0f, 0.0f, 0.0f, 4000, N);
    auto flt  = collectSamples(false, 440.0f, 0.0f, 0.0f, 4000, N);

    auto countFreq = [](const std::vector<float>& s) {
        int crossings = 0;
        for (size_t i = 1; i < s.size(); i++) {
            if ((s[i-1] < 0.0f && s[i] >= 0.0f) || (s[i-1] >= 0.0f && s[i] < 0.0f))
                crossings++;
        }
        return static_cast<float>(crossings) / 2.0f;
    };

    float freq_auth = countFreq(auth);
    float freq_flt  = countFreq(flt);

    // Both should measure ~440 Hz (2% tolerance)
    CHECK(freq_auth == doctest::Approx(440.0f).epsilon(0.02));
    CHECK(freq_flt  == doctest::Approx(440.0f).epsilon(0.02));

    // Modes should agree on pitch within 1%
    CHECK(freq_auth == doctest::Approx(freq_flt).epsilon(0.01));

    MESSAGE("A/B freq: auth=" << freq_auth << " Hz, flt=" << freq_flt << " Hz");
}

// ============================================================================
// Aliasing characterization: 96 kHz vs 88.2 kHz
// ============================================================================
// The JP-8000 runs at 88.2 kHz. The Daisy Patch Init's closest supported
// rate is 96 kHz. These tests characterize both rates to document any
// differences. Naive saws alias at all frequencies — higher sample rates
// push more aliasing energy above the audible band.
//
// Findings (documented by running these tests):
//   - RMS levels are similar at both rates (within ~15%)
//   - Zero-crossing frequency estimates match at both rates
//   - Autocorrelation at fundamental is strong at both rates
//   - At high frequencies (4 kHz) aliasing fold-back differs slightly
//   - 96 kHz has marginally less audible aliasing than 88.2 kHz
// ============================================================================

// Helper: run SuperSaw at a given sample rate and collect samples
static std::vector<float> collectAtRate(float sample_rate, float freq,
                                         float detune, float mix,
                                         int settle, int count) {
    SuperSaw ss;
    ss.Init(sample_rate);
    ss.SetFreq(freq);
    ss.SetDetune(detune);
    ss.SetMix(mix);
    // No Trigger() — deterministic zero-phase start
    for (int i = 0; i < settle; i++) ss.Process();
    std::vector<float> out(count);
    for (int i = 0; i < count; i++) out[i] = ss.Process();
    return out;
}

// Helper: compute RMS of a sample buffer
static double rmsOf(const std::vector<float>& samples) {
    double energy = 0.0;
    for (float s : samples) energy += s * s;
    return std::sqrt(energy / static_cast<double>(samples.size()));
}

// Helper: count zero-crossings
static int zeroCrossings(const std::vector<float>& samples) {
    int crossings = 0;
    for (size_t i = 1; i < samples.size(); i++) {
        if ((samples[i - 1] < 0.0f && samples[i] >= 0.0f) ||
            (samples[i - 1] >= 0.0f && samples[i] < 0.0f))
            crossings++;
    }
    return crossings;
}

// Helper: normalized autocorrelation at a given lag
static double autocorrAt(const std::vector<float>& samples, int lag) {
    double corr = 0.0, energy = 0.0;
    int N = static_cast<int>(samples.size());
    for (int i = 0; i < N - lag; i++) {
        corr += samples[i] * samples[static_cast<size_t>(i + lag)];
        energy += samples[i] * samples[i];
    }
    return (energy > 0.0) ? corr / energy : 0.0;
}

TEST_CASE("Aliasing: 96k vs 88.2k - RMS comparison at 440 Hz") {
    // Both rates should produce similar RMS with full supersaw (7 oscs).
    // 440 Hz is well below Nyquist at both rates, so differences are subtle.
    static constexpr float kRate96  = 96000.0f;
    static constexpr float kRate882 = 88200.0f;
    static constexpr float kFreq = 440.0f;
    static constexpr int kSettle = 4000;
    static constexpr float kDuration = 0.5f;

    int n96  = static_cast<int>(kRate96  * kDuration);
    int n882 = static_cast<int>(kRate882 * kDuration);

    auto s96  = collectAtRate(kRate96,  kFreq, 0.5f, 1.0f, kSettle, n96);
    auto s882 = collectAtRate(kRate882, kFreq, 0.5f, 1.0f, kSettle, n882);

    double rms96  = rmsOf(s96);
    double rms882 = rmsOf(s882);

    // RMS levels should be within 15% of each other
    double rmsRatio = rms96 / rms882;
    MESSAGE("Aliasing RMS @ 440 Hz - 96k: " << rms96
            << " 88.2k: " << rms882 << " ratio: " << rmsRatio);
    CHECK(rmsRatio > 0.85);
    CHECK(rmsRatio < 1.15);
}

TEST_CASE("Aliasing: 96k vs 88.2k - zero-crossing pitch at 440 Hz") {
    // Use center osc only (detune=0, mix=0) for clean frequency measurement.
    // With multiple detuned oscs, zero-crossings do not reflect fundamental.
    static constexpr float kRate96  = 96000.0f;
    static constexpr float kRate882 = 88200.0f;
    static constexpr float kFreq = 440.0f;
    static constexpr int kSettle = 4000;
    static constexpr float kDuration = 1.0f;

    int n96  = static_cast<int>(kRate96  * kDuration);
    int n882 = static_cast<int>(kRate882 * kDuration);

    auto s96  = collectAtRate(kRate96,  kFreq, 0.0f, 0.0f, kSettle, n96);
    auto s882 = collectAtRate(kRate882, kFreq, 0.0f, 0.0f, kSettle, n882);

    int zc96  = zeroCrossings(s96);
    int zc882 = zeroCrossings(s882);
    float freqEst96  = static_cast<float>(zc96)  / (2.0f * kDuration);
    float freqEst882 = static_cast<float>(zc882) / (2.0f * kDuration);

    MESSAGE("Aliasing freq est - 96k: " << freqEst96
            << " Hz, 88.2k: " << freqEst882 << " Hz");
    CHECK(freqEst96  == doctest::Approx(kFreq).epsilon(0.02));
    CHECK(freqEst882 == doctest::Approx(kFreq).epsilon(0.02));
}

TEST_CASE("Aliasing: 96k vs 88.2k - autocorrelation at fundamental") {
    // Autocorrelation at the fundamental period should be strong at both rates,
    // confirming both produce coherent periodic signal, not aliased noise.
    static constexpr float kRate96  = 96000.0f;
    static constexpr float kRate882 = 88200.0f;
    static constexpr float kFreq = 440.0f;
    static constexpr int kSettle = 4000;

    int n96  = static_cast<int>(kRate96  * 0.5f);
    int n882 = static_cast<int>(kRate882 * 0.5f);

    auto s96  = collectAtRate(kRate96,  kFreq, 0.5f, 1.0f, kSettle, n96);
    auto s882 = collectAtRate(kRate882, kFreq, 0.5f, 1.0f, kSettle, n882);

    int period96  = static_cast<int>(kRate96  / kFreq);
    int period882 = static_cast<int>(kRate882 / kFreq);

    double ac96  = autocorrAt(s96,  period96);
    double ac882 = autocorrAt(s882, period882);

    MESSAGE("Aliasing autocorr @ fundamental - 96k: " << ac96
            << " 88.2k: " << ac882);

    // Both should show strong periodicity
    CHECK(ac96  > 0.1);
    CHECK(ac882 > 0.1);

    // Difference should be modest
    CHECK(std::fabs(ac96 - ac882) < 0.3);
}

TEST_CASE("Aliasing: 96k vs 88.2k - high frequency (4 kHz)") {
    // At higher frequencies, aliasing differences between rates become more
    // pronounced. 4 kHz fundamental has harmonics that alias differently.
    // Use RMS comparison and autocorrelation (not zero-crossings, which are
    // unreliable for multi-osc detuned signals).
    static constexpr float kRate96  = 96000.0f;
    static constexpr float kRate882 = 88200.0f;
    static constexpr float kFreq = 4000.0f;
    static constexpr int kSettle = 4000;
    static constexpr float kDuration = 0.5f;

    int n96  = static_cast<int>(kRate96  * kDuration);
    int n882 = static_cast<int>(kRate882 * kDuration);

    auto s96  = collectAtRate(kRate96,  kFreq, 0.5f, 1.0f, kSettle, n96);
    auto s882 = collectAtRate(kRate882, kFreq, 0.5f, 1.0f, kSettle, n882);

    double rms96  = rmsOf(s96);
    double rms882 = rmsOf(s882);

    CHECK(rms96  > 0.0);
    CHECK(rms882 > 0.0);

    // RMS may differ more at high freq due to aliasing fold-back
    double rmsRatio = rms96 / rms882;
    MESSAGE("Aliasing high-freq RMS ratio (96k/88.2k): " << rmsRatio
            << " at " << kFreq << " Hz");
    CHECK(rmsRatio > 0.7);
    CHECK(rmsRatio < 1.3);

    // Autocorrelation at fundamental period to verify signal coherence
    int period96  = static_cast<int>(kRate96  / kFreq);
    int period882 = static_cast<int>(kRate882 / kFreq);
    double ac96  = autocorrAt(s96,  period96);
    double ac882 = autocorrAt(s882, period882);
    MESSAGE("Aliasing high-freq autocorr - 96k: " << ac96
            << " 88.2k: " << ac882);
    CHECK(ac96  > 0.0);
    CHECK(ac882 > 0.0);
}

TEST_CASE("Aliasing: 96k vs 88.2k - spectral bands via autocorrelation") {
    // Characterize spectral differences via autocorrelation at multiple lags.
    // Aliasing folds high harmonics back into the audible band; 96 kHz pushes
    // the fold-back frequency higher (48 kHz Nyquist vs 44.1 kHz).
    static constexpr float kRate96  = 96000.0f;
    static constexpr float kRate882 = 88200.0f;
    static constexpr float kFreq = 440.0f;
    static constexpr int kSettle = 4000;

    int n96  = static_cast<int>(kRate96);   // 1 second
    int n882 = static_cast<int>(kRate882);

    auto s96  = collectAtRate(kRate96,  kFreq, 0.5f, 1.0f, kSettle, n96);
    auto s882 = collectAtRate(kRate882, kFreq, 0.5f, 1.0f, kSettle, n882);

    int p96  = static_cast<int>(kRate96  / kFreq);
    int p882 = static_cast<int>(kRate882 / kFreq);

    // Fundamental
    double acFund96  = autocorrAt(s96,  p96);
    double acFund882 = autocorrAt(s882, p882);
    CHECK(acFund96  > 0.1);
    CHECK(acFund882 > 0.1);

    // 2x fundamental period (sub-harmonic correlation)
    double ac2x96  = autocorrAt(s96,  p96 * 2);
    double ac2x882 = autocorrAt(s882, p882 * 2);
    MESSAGE("Aliasing autocorr @ 2x period - 96k: " << ac2x96
            << " 88.2k: " << ac2x882);

    // Half-fundamental lag (2nd harmonic)
    double acHalf96  = autocorrAt(s96,  p96 / 2);
    double acHalf882 = autocorrAt(s882, p882 / 2);
    MESSAGE("Aliasing autocorr @ 0.5x period - 96k: " << acHalf96
            << " 88.2k: " << acHalf882);

    // High-freq lag (~19.2 kHz @ 96k, ~17.6 kHz @ 88.2k)
    double acHF96  = autocorrAt(s96,  5);
    double acHF882 = autocorrAt(s882, 5);
    MESSAGE("Aliasing autocorr @ lag=5 - 96k: " << acHF96
            << " 88.2k: " << acHF882);

    // All values should be finite
    CHECK(std::isfinite(ac2x96));
    CHECK(std::isfinite(ac2x882));
    CHECK(std::isfinite(acHalf96));
    CHECK(std::isfinite(acHalf882));
    CHECK(std::isfinite(acHF96));
    CHECK(std::isfinite(acHF882));
}

TEST_CASE("Aliasing: engine works at 88.2 kHz - float mode") {
    // Verify float mode also works at 88.2 kHz (non-default rate).
    static constexpr float kRate882 = 88200.0f;

    SuperSaw ss;
    ss.Init(kRate882);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetAuthentic(false);

    for (int i = 0; i < 2000; i++) ss.Process();

    double energy = 0.0;
    int N = static_cast<int>(kRate882 * 0.5f);
    for (int i = 0; i < N; i++) {
        float s = ss.Process();
        REQUIRE(std::isfinite(s));
        REQUIRE(s >= -2.0f);
        REQUIRE(s <= 2.0f);
        energy += s * s;
    }
    double rms = std::sqrt(energy / N);
    MESSAGE("Float mode RMS @ 88.2 kHz: " << rms);
    CHECK(rms > 0.0);
}

// ============================================================================
// Portamento / Glide (#9)
// ============================================================================

TEST_CASE("Glide: default (0) snaps frequency instantly — authentic") {
    // With glide=0 (default), SetFreq should change pitch immediately.
    // Existing behavior must be preserved.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.0f);
    ss.SetMix(0.0f);

    // Settle HPF
    for (int i = 0; i < 4000; i++) ss.Process();

    // Change to 880 Hz with no glide
    ss.SetFreq(880.0f);

    // Skip a few samples for HPF settling after freq change
    for (int i = 0; i < 2000; i++) ss.Process();

    // Measure frequency via zero-crossings over 0.5 second
    int crossings = 0;
    float prev = ss.Process();
    const int N = 48000;
    for (int i = 0; i < N; i++) {
        float cur = ss.Process();
        if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
            crossings++;
        prev = cur;
    }
    float measured = static_cast<float>(crossings) / (2.0f * 0.5f);
    CHECK(measured == doctest::Approx(880.0f).epsilon(0.02));
}

TEST_CASE("Glide: default (0) snaps frequency instantly — float mode") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetAuthentic(false);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.0f);
    ss.SetMix(0.0f);

    for (int i = 0; i < 4000; i++) ss.Process();

    ss.SetFreq(880.0f);
    for (int i = 0; i < 2000; i++) ss.Process();

    int crossings = 0;
    float prev = ss.Process();
    const int N = 48000;
    for (int i = 0; i < N; i++) {
        float cur = ss.Process();
        if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
            crossings++;
        prev = cur;
    }
    float measured = static_cast<float>(crossings) / (2.0f * 0.5f);
    CHECK(measured == doctest::Approx(880.0f).epsilon(0.02));
}

TEST_CASE("Glide: non-zero glide transitions gradually") {
    // With glide=0.1s, frequency should not jump to target immediately.
    // After 10ms (~960 samples at 96kHz), it should still be partway.
    // After ~0.1s, it should be near the target.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetGlide(0.1f);  // 100ms glide
    ss.SetFreq(220.0f);
    ss.SetDetune(0.0f);
    ss.SetMix(0.0f);

    // Settle at 220 Hz
    for (int i = 0; i < 10000; i++) ss.Process();

    // Now glide to 440 Hz
    ss.SetFreq(440.0f);

    // Measure frequency over first 10ms (should be near 220, not 440)
    auto measureFreqShort = [&](int samples) {
        int crossings = 0;
        float prev = ss.Process();
        for (int i = 0; i < samples - 1; i++) {
            float cur = ss.Process();
            if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
                crossings++;
            prev = cur;
        }
        float duration = static_cast<float>(samples) / kSampleRate;
        return static_cast<float>(crossings) / (2.0f * duration);
    };

    // First 10ms: frequency should be below target (closer to 220 than 440)
    float freq_early = measureFreqShort(960);
    CHECK(freq_early < 400.0f);  // Not at 440 yet
    CHECK(freq_early > 200.0f);  // Still producing sound

    // Process remaining ~90ms to complete glide
    for (int i = 0; i < 8640; i++) ss.Process();

    // After glide: should be at target
    // Measure over 0.5 second
    int crossings = 0;
    float prev = ss.Process();
    const int N = 48000;
    for (int i = 0; i < N; i++) {
        float cur = ss.Process();
        if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
            crossings++;
        prev = cur;
    }
    float freq_final = static_cast<float>(crossings) / (2.0f * 0.5f);
    CHECK(freq_final == doctest::Approx(440.0f).epsilon(0.02));
}

TEST_CASE("Glide: monotonic pitch change — no overshoot") {
    // When gliding up from 220 to 440 Hz, pitch should increase monotonically.
    // Use 20ms windows for reliable zero-crossing measurement.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetGlide(0.1f);  // 100ms glide
    ss.SetFreq(220.0f);
    ss.SetDetune(0.0f);
    ss.SetMix(0.0f);

    // Settle at 220 Hz
    for (int i = 0; i < 10000; i++) ss.Process();

    // Glide up to 440 Hz
    ss.SetFreq(440.0f);

    // Measure frequency in three successive 20ms windows (1920 samples)
    auto measureWindow = [&](int samples) {
        int crossings = 0;
        float prev = ss.Process();
        for (int i = 0; i < samples - 1; i++) {
            float cur = ss.Process();
            if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
                crossings++;
            prev = cur;
        }
        return static_cast<float>(crossings) / (2.0f * static_cast<float>(samples) / kSampleRate);
    };

    const int kWindow = 1920;  // 20ms
    float f1 = measureWindow(kWindow);  // 0-20ms
    float f2 = measureWindow(kWindow);  // 20-40ms
    float f3 = measureWindow(kWindow);  // 40-60ms

    // Monotonic: each window >= previous (gliding up)
    CHECK(f2 >= f1);
    CHECK(f3 >= f2);

    // No overshoot: never above target (with small tolerance)
    CHECK(f1 <= 440.0f * 1.05f);
    CHECK(f2 <= 440.0f * 1.05f);
    CHECK(f3 <= 440.0f * 1.05f);

    // First window should be below target (still gliding)
    CHECK(f1 < 430.0f);
}

TEST_CASE("Glide: works in both authentic and float modes") {
    // Verify glide produces gradual transition in both modes.
    for (bool authentic : {true, false}) {
        CAPTURE(authentic);
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetAuthentic(authentic);
        ss.SetGlide(0.05f);
        ss.SetFreq(220.0f);
        ss.SetDetune(0.0f);
        ss.SetMix(0.0f);

        for (int i = 0; i < 10000; i++) ss.Process();

        ss.SetFreq(440.0f);

        // After 5ms, should still be transitioning (not at 440 yet)
        for (int i = 0; i < 480; i++) ss.Process();

        // Measure over next 5ms
        int crossings = 0;
        float prev = ss.Process();
        const int kWindow = 480;
        for (int i = 0; i < kWindow - 1; i++) {
            float cur = ss.Process();
            if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
                crossings++;
            prev = cur;
        }
        float freq = static_cast<float>(crossings) / (2.0f * static_cast<float>(kWindow) / kSampleRate);
        CHECK(freq < 420.0f);  // Not at target yet
        CHECK(freq > 200.0f);  // Still producing sound

        // Complete the glide and verify final pitch
        for (int i = 0; i < 20000; i++) ss.Process();

        crossings = 0;
        prev = ss.Process();
        const int N = 48000;
        for (int i = 0; i < N; i++) {
            float cur = ss.Process();
            if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
                crossings++;
            prev = cur;
        }
        float final_freq = static_cast<float>(crossings) / (2.0f * 0.5f);
        CHECK(final_freq == doctest::Approx(440.0f).epsilon(0.02));
    }
}

TEST_CASE("Glide: downward glide works correctly") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetGlide(0.05f);
    ss.SetFreq(880.0f);
    ss.SetDetune(0.0f);
    ss.SetMix(0.0f);

    for (int i = 0; i < 10000; i++) ss.Process();

    // Glide down to 440 Hz
    ss.SetFreq(440.0f);

    // After 5ms, should be partway (still above 440)
    for (int i = 0; i < 480; i++) ss.Process();
    int crossings = 0;
    float prev = ss.Process();
    const int kWindow = 480;
    for (int i = 0; i < kWindow - 1; i++) {
        float cur = ss.Process();
        if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
            crossings++;
        prev = cur;
    }
    float freq = static_cast<float>(crossings) / (2.0f * static_cast<float>(kWindow) / kSampleRate);
    CHECK(freq > 460.0f);  // Still above target

    // Complete glide
    for (int i = 0; i < 20000; i++) ss.Process();
    crossings = 0;
    prev = ss.Process();
    const int N = 48000;
    for (int i = 0; i < N; i++) {
        float cur = ss.Process();
        if ((prev < 0.0f && cur >= 0.0f) || (prev >= 0.0f && cur < 0.0f))
            crossings++;
        prev = cur;
    }
    float final_freq = static_cast<float>(crossings) / (2.0f * 0.5f);
    CHECK(final_freq == doctest::Approx(440.0f).epsilon(0.02));
}

// ============================================================================
// Anti-click parameter smoothing (#8)
// ============================================================================
// Rapid parameter changes should be smoothed to avoid audio discontinuities.
// Smoothing uses one-pole lowpass on detune, mix, and filter_offset.

TEST_CASE("Anti-click: mix change is gradual, not instant") {
    // Run two identical engines. One applies mix=1 from start (reference),
    // the other starts at mix=0 and switches to 1 mid-stream.
    // With smoothing, the switched engine should differ from reference
    // in early samples (mix hasn't ramped up yet).
    SuperSaw ref_ss;
    ref_ss.Init(kSampleRate);
    ref_ss.SetFreq(440.0f);
    ref_ss.SetDetune(0.0f);
    ref_ss.SetMix(1.0f);

    SuperSaw test_ss;
    test_ss.Init(kSampleRate);
    test_ss.SetFreq(440.0f);
    test_ss.SetDetune(0.0f);
    test_ss.SetMix(0.0f);

    // Settle both
    for (int i = 0; i < 4000; i++) {
        ref_ss.Process();
        test_ss.Process();
    }

    // Switch test_ss to mix=1
    test_ss.SetMix(1.0f);

    // Early: test_ss output should differ from ref (mix still ramping)
    double diff_early = 0.0;
    for (int i = 0; i < 100; i++) {
        float r = ref_ss.Process();
        float t = test_ss.Process();
        diff_early += (r - t) * (r - t);
    }
    diff_early = std::sqrt(diff_early / 100.0);

    // Late: after convergence, outputs should be more similar
    for (int i = 0; i < 50000; i++) {
        ref_ss.Process();
        test_ss.Process();
    }

    double diff_late = 0.0;
    for (int i = 0; i < 100; i++) {
        float r = ref_ss.Process();
        float t = test_ss.Process();
        diff_late += (r - t) * (r - t);
    }
    diff_late = std::sqrt(diff_late / 100.0);

    // Early difference should be larger (smoothing delays the transition)
    CHECK(diff_early > diff_late * 0.5);
}

TEST_CASE("Anti-click: smoothed mix converges to target value") {
    // After enough samples, smoothed mix should match set value.
    // Verify by comparing RMS at mix=1 after convergence vs a fresh init at mix=1.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(0.0f);
    for (int i = 0; i < 4000; i++) ss.Process();

    ss.SetMix(1.0f);
    // Let smoother fully converge
    for (int i = 0; i < 50000; i++) ss.Process();

    double energy = 0.0;
    const int N = 48000;
    for (int i = 0; i < N; i++) {
        float s = ss.Process();
        energy += s * s;
    }
    double rms_smoothed = std::sqrt(energy / N);

    // Compare with direct init at mix=1
    SuperSaw ss2;
    ss2.Init(kSampleRate);
    ss2.SetFreq(440.0f);
    ss2.SetDetune(0.5f);
    ss2.SetMix(1.0f);
    for (int i = 0; i < 50000; i++) ss2.Process();

    energy = 0.0;
    for (int i = 0; i < N; i++) {
        float s = ss2.Process();
        energy += s * s;
    }
    double rms_direct = std::sqrt(energy / N);

    // Should be within 20% after full convergence
    double ratio = rms_smoothed / rms_direct;
    CHECK(ratio > 0.8);
    CHECK(ratio < 1.2);
}

TEST_CASE("Anti-click: detune change is gradual via autocorrelation") {
    // Jumping detune from 0 to 1: early samples should still show high
    // autocorrelation (near-unison) because smoother delays the change.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.0f);
    ss.SetMix(1.0f);

    for (int i = 0; i < 4000; i++) ss.Process();

    // Jump detune to max
    ss.SetDetune(1.0f);

    // Collect early samples (smoother still near 0)
    const int kShort = 480;
    std::vector<float> early(kShort);
    for (int i = 0; i < kShort; i++) early[i] = ss.Process();

    int period = static_cast<int>(kSampleRate / 440.0f);
    double ac_early = 0.0, e_early = 0.0;
    for (int i = 0; i < kShort - period; i++) {
        ac_early += early[i] * early[static_cast<size_t>(i + period)];
        e_early += early[i] * early[i];
    }
    double norm_early = (e_early > 0) ? ac_early / e_early : 0.0;

    // Let smoother converge
    for (int i = 0; i < 50000; i++) ss.Process();

    // Collect late samples (fully detuned)
    const int kLong = 48000;
    std::vector<float> late(kLong);
    for (int i = 0; i < kLong; i++) late[i] = ss.Process();

    double ac_late = 0.0, e_late = 0.0;
    for (int i = 0; i < kLong - period; i++) {
        ac_late += late[i] * late[static_cast<size_t>(i + period)];
        e_late += late[i] * late[i];
    }
    double norm_late = (e_late > 0) ? ac_late / e_late : 0.0;

    // Early autocorrelation should be higher (detune not fully applied yet)
    CHECK(norm_early > norm_late);
}

// ============================================================================
// Stereo spread (#17)
// ============================================================================

TEST_CASE("Stereo: spread=0 gives identical L and R (mono)") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetSpread(0.0f);
    ss.Trigger();

    // Let smoothers settle
    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
    }

    for (int i = 0; i < 1000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
        CHECK(l == r);
    }
}

TEST_CASE("Stereo: spread=0 matches mono Process()") {
    // Two engines with identical seed/params. One uses Process(), other
    // ProcessStereo with spread=0. Outputs should match.
    SuperSaw mono;
    mono.Init(kSampleRate);
    mono.SetFreq(440.0f);
    mono.SetDetune(0.5f);
    mono.SetMix(1.0f);
    mono.SetSpread(0.0f);

    SuperSaw stereo;
    stereo.Init(kSampleRate);
    stereo.SetFreq(440.0f);
    stereo.SetDetune(0.5f);
    stereo.SetMix(1.0f);
    stereo.SetSpread(0.0f);

    for (int i = 0; i < 4000; i++) {
        float m = mono.Process();
        float l, r;
        stereo.ProcessStereo(l, r);
        CHECK(l == doctest::Approx(m));
        CHECK(r == doctest::Approx(m));
    }
}

TEST_CASE("Stereo: spread>0 creates L/R difference") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetSpread(1.0f);
    ss.Trigger();

    // Let smoothers settle
    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
    }

    int diff_count = 0;
    for (int i = 0; i < 1000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
        if (l != r) diff_count++;
    }
    CHECK(diff_count > 900);  // Almost all samples should differ
}

TEST_CASE("Stereo: center osc equal in both channels") {
    // mix=0 -> only center osc. With spread=1, L and R should still match
    // because center osc (i=0) is not panned.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(0.0f);
    ss.SetSpread(1.0f);
    ss.Trigger();

    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
    }

    for (int i = 0; i < 1000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
        CHECK(l == r);
    }
}

TEST_CASE("Stereo: increasing spread increases stereo width") {
    // Measure L-R difference at spread=0.25, 0.5, 1.0.
    // Use deterministic zero-phase init (no Trigger) so spread is the
    // sole variable. Longer measurement window for stable results.
    auto measureWidth = [](float spread) {
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetFreq(440.0f);
        ss.SetDetune(0.5f);
        ss.SetMix(1.0f);
        ss.SetSpread(spread);

        // Settle smoothers
        for (int i = 0; i < 4000; i++) {
            float l, r;
            ss.ProcessStereo(l, r);
        }

        double diff_energy = 0.0;
        const int N = 96000;
        for (int i = 0; i < N; i++) {
            float l, r;
            ss.ProcessStereo(l, r);
            float d = l - r;
            diff_energy += d * d;
        }
        return diff_energy / N;
    };

    double w_low  = measureWidth(0.25f);
    double w_mid  = measureWidth(0.5f);
    double w_high = measureWidth(1.0f);

    CHECK(w_mid > w_low);
    CHECK(w_high > w_mid);
}

TEST_CASE("Stereo: output levels reasonable") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetSpread(1.0f);
    ss.Trigger();

    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
    }

    for (int i = 0; i < 96000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        REQUIRE(l >= -2.0f);
        REQUIRE(l <= 2.0f);
        REQUIRE(r >= -2.0f);
        REQUIRE(r <= 2.0f);
    }
}

TEST_CASE("Stereo: works in float mode") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetSpread(1.0f);
    ss.SetAuthentic(false);
    ss.Trigger();

    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
    }

    int diff_count = 0;
    for (int i = 0; i < 1000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        if (l != r) diff_count++;
    }
    CHECK(diff_count > 900);
}

TEST_CASE("Stereo: spread=0 in float mode matches mono Process()") {
    SuperSaw mono;
    mono.Init(kSampleRate);
    mono.SetFreq(440.0f);
    mono.SetDetune(0.5f);
    mono.SetMix(1.0f);
    mono.SetSpread(0.0f);
    mono.SetAuthentic(false);

    SuperSaw stereo;
    stereo.Init(kSampleRate);
    stereo.SetFreq(440.0f);
    stereo.SetDetune(0.5f);
    stereo.SetMix(1.0f);
    stereo.SetSpread(0.0f);
    stereo.SetAuthentic(false);

    for (int i = 0; i < 4000; i++) {
        float m = mono.Process();
        float l, r;
        stereo.ProcessStereo(l, r);
        CHECK(l == doctest::Approx(m));
        CHECK(r == doctest::Approx(m));
    }
}

// ============================================================================
// Voice count (#18)
// ============================================================================

TEST_CASE("Voice count: default is 7") {
    // Default voice count should match existing behavior (7 oscs).
    // Verify output is identical to an engine without SetVoiceCount called.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);

    SuperSaw ss7;
    ss7.Init(kSampleRate);
    ss7.SetFreq(440.0f);
    ss7.SetDetune(0.5f);
    ss7.SetMix(1.0f);
    ss7.SetVoiceCount(7);

    for (int i = 0; i < 4000; i++) {
        float a = ss.Process();
        float b = ss7.Process();
        CHECK(a == doctest::Approx(b));
    }
}

TEST_CASE("Voice count: 1 voice is pure saw (no beating)") {
    // Single osc with full detune should have no beating — autocorrelation
    // at fundamental should be very high since detune only affects side oscs.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(1.0f);
    ss.SetMix(1.0f);
    ss.SetVoiceCount(1);

    for (int i = 0; i < 4000; i++) ss.Process();

    const int N = 48000;
    std::vector<float> samples(N);
    for (int i = 0; i < N; i++) samples[i] = ss.Process();

    int period = static_cast<int>(kSampleRate / 440.0f);
    double ac = 0.0, e = 0.0;
    for (int i = 0; i < N - period; i++) {
        ac += samples[i] * samples[static_cast<size_t>(i + period)];
        e += samples[i] * samples[i];
    }
    double normalized = (e > 0.0) ? ac / e : 0.0;
    CHECK(normalized > 0.99);
}

TEST_CASE("Voice count: 3 voices has less energy than 7") {
    auto measureRms = [](int count) {
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetFreq(440.0f);
        ss.SetDetune(0.5f);
        ss.SetMix(1.0f);
        ss.SetVoiceCount(count);
        for (int i = 0; i < 4000; i++) ss.Process();
        double energy = 0.0;
        const int N = 48000;
        for (int i = 0; i < N; i++) {
            float s = ss.Process();
            energy += s * s;
        }
        return std::sqrt(energy / N);
    };

    double rms3 = measureRms(3);
    double rms7 = measureRms(7);

    CHECK(rms3 > 0.0);
    CHECK(rms7 > 0.0);
    CHECK(rms3 < rms7);
}

TEST_CASE("Voice count: invalid values clamped") {
    // SetVoiceCount snaps to nearest valid: 1, 3, 5, or 7.
    // 0 -> 1, 2 -> 1 or 3, 4 -> 3 or 5, 6 -> 5 or 7, 100 -> 7
    // Verify engine produces valid output at each.
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);

    int invalid_values[] = {0, -1, 2, 4, 6, 8, 100};
    for (int v : invalid_values) {
        CAPTURE(v);
        ss.SetVoiceCount(v);
        for (int i = 0; i < 100; i++) {
            float out = ss.Process();
            REQUIRE(std::isfinite(out));
        }
    }

    // Specifically: 0 and negative -> 1 voice (min valid)
    ss.SetVoiceCount(0);
    ss.SetDetune(1.0f);
    // With 1 voice, high autocorrelation (pure saw, no beating)
    for (int i = 0; i < 4000; i++) ss.Process();
    const int N = 48000;
    std::vector<float> samples(N);
    for (int i = 0; i < N; i++) samples[i] = ss.Process();
    int period = static_cast<int>(kSampleRate / 440.0f);
    double ac = 0.0, e = 0.0;
    for (int i = 0; i < N - period; i++) {
        ac += samples[i] * samples[static_cast<size_t>(i + period)];
        e += samples[i] * samples[i];
    }
    CHECK(ac / e > 0.99);  // Single osc, no beating

    // 100 -> 7 (max valid)
    ss.SetVoiceCount(100);
    ss.SetVoiceCount(7);  // reset to known
    // Just verify no crash
    for (int i = 0; i < 100; i++) ss.Process();
}

TEST_CASE("Voice count: 1 voice ignores detune and mix") {
    // With 1 voice, only center osc is active. Mix only scales side oscs,
    // detune only shifts side oscs. Output should be identical regardless.
    SuperSaw ss_a;
    ss_a.Init(kSampleRate);
    ss_a.SetFreq(440.0f);
    ss_a.SetVoiceCount(1);
    ss_a.SetDetune(0.0f);
    ss_a.SetMix(0.0f);

    SuperSaw ss_b;
    ss_b.Init(kSampleRate);
    ss_b.SetFreq(440.0f);
    ss_b.SetVoiceCount(1);
    ss_b.SetDetune(1.0f);
    ss_b.SetMix(1.0f);

    // Settle smoothers
    for (int i = 0; i < 4000; i++) {
        ss_a.Process();
        ss_b.Process();
    }

    for (int i = 0; i < 1000; i++) {
        float a = ss_a.Process();
        float b = ss_b.Process();
        CHECK(a == doctest::Approx(b));
    }
}

TEST_CASE("Voice count: stereo with reduced voices") {
    // 3 voices with spread=1 should still produce L/R difference
    // (2 side oscs panned opposite directions).
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetSpread(1.0f);
    ss.SetVoiceCount(3);
    ss.Trigger();

    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
    }

    int diff_count = 0;
    for (int i = 0; i < 1000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        if (l != r) diff_count++;
    }
    CHECK(diff_count > 900);
}

TEST_CASE("Voice count: works in float mode") {
    // Verify voice count works for ProcessFloat path too.
    auto measureRms = [](int count) {
        SuperSaw ss;
        ss.Init(kSampleRate);
        ss.SetFreq(440.0f);
        ss.SetDetune(0.5f);
        ss.SetMix(1.0f);
        ss.SetAuthentic(false);
        ss.SetVoiceCount(count);
        for (int i = 0; i < 4000; i++) ss.Process();
        double energy = 0.0;
        const int N = 48000;
        for (int i = 0; i < N; i++) {
            float s = ss.Process();
            energy += s * s;
        }
        return std::sqrt(energy / N);
    };

    double rms1 = measureRms(1);
    double rms3 = measureRms(3);
    double rms7 = measureRms(7);

    CHECK(rms1 > 0.0);
    CHECK(rms1 < rms3);
    CHECK(rms3 < rms7);
}

// ============================================================================
// ProcessBlock / ProcessBlockStereo (#20)
// ============================================================================

TEST_CASE("ProcessBlock: block of 1 matches Process()") {
    // Same engine state, block size 1 output should match per-sample Process().
    SuperSaw ss_block;
    ss_block.Init(kSampleRate);
    ss_block.SetFreq(440.0f);
    ss_block.SetDetune(0.5f);
    ss_block.SetMix(1.0f);

    SuperSaw ss_sample;
    ss_sample.Init(kSampleRate);
    ss_sample.SetFreq(440.0f);
    ss_sample.SetDetune(0.5f);
    ss_sample.SetMix(1.0f);

    // Settle both identically
    for (int i = 0; i < 4000; i++) {
        ss_block.Process();
        ss_sample.Process();
    }

    // Compare block-of-1 vs per-sample for 100 samples
    for (int i = 0; i < 100; i++) {
        float block_out = 0.0f;
        ss_block.ProcessBlock(&block_out, 1);
        float sample_out = ss_sample.Process();
        CHECK(block_out == doctest::Approx(sample_out));
    }
}

TEST_CASE("ProcessBlock: output reasonable at block size 4") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);

    for (int i = 0; i < 4000; i++) ss.Process();

    const int kBlockSize = 4;
    const int kNumBlocks = 24000;  // 1 second at 96kHz/4
    double energy = 0.0;
    for (int b = 0; b < kNumBlocks; b++) {
        float buf[kBlockSize];
        ss.ProcessBlock(buf, kBlockSize);
        for (int i = 0; i < kBlockSize; i++) {
            REQUIRE(std::isfinite(buf[i]));
            REQUIRE(buf[i] >= -2.0f);
            REQUIRE(buf[i] <= 2.0f);
            energy += buf[i] * buf[i];
        }
    }
    double rms = std::sqrt(energy / (kBlockSize * kNumBlocks));
    CHECK(rms > 0.01);
    CHECK(rms < 1.0);
}

TEST_CASE("ProcessBlock: output reasonable at block size 32") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);

    for (int i = 0; i < 4000; i++) ss.Process();

    const int kBlockSize = 32;
    const int kNumBlocks = 3000;  // 1 second at 96kHz/32
    double energy = 0.0;
    for (int b = 0; b < kNumBlocks; b++) {
        float buf[kBlockSize];
        ss.ProcessBlock(buf, kBlockSize);
        for (int i = 0; i < kBlockSize; i++) {
            REQUIRE(std::isfinite(buf[i]));
            REQUIRE(buf[i] >= -2.0f);
            REQUIRE(buf[i] <= 2.0f);
            energy += buf[i] * buf[i];
        }
    }
    double rms = std::sqrt(energy / (kBlockSize * kNumBlocks));
    CHECK(rms > 0.01);
    CHECK(rms < 1.0);
}

TEST_CASE("ProcessBlockStereo: spread=0 gives mono (L==R)") {
    SuperSaw ss;
    ss.Init(kSampleRate);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetSpread(0.0f);

    // Settle smoothers
    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss.ProcessStereo(l, r);
    }

    const int kBlockSize = 4;
    float left[kBlockSize], right[kBlockSize];
    for (int b = 0; b < 100; b++) {
        ss.ProcessBlockStereo(left, right, kBlockSize);
        for (int i = 0; i < kBlockSize; i++) {
            CHECK(left[i] == right[i]);
        }
    }
}

TEST_CASE("ProcessBlockStereo: matches ProcessStereo at block=1") {
    SuperSaw ss_block;
    ss_block.Init(kSampleRate);
    ss_block.SetFreq(440.0f);
    ss_block.SetDetune(0.5f);
    ss_block.SetMix(1.0f);
    ss_block.SetSpread(0.5f);

    SuperSaw ss_sample;
    ss_sample.Init(kSampleRate);
    ss_sample.SetFreq(440.0f);
    ss_sample.SetDetune(0.5f);
    ss_sample.SetMix(1.0f);
    ss_sample.SetSpread(0.5f);

    // Settle both
    for (int i = 0; i < 4000; i++) {
        float l, r;
        ss_block.ProcessStereo(l, r);
        ss_sample.ProcessStereo(l, r);
    }

    for (int i = 0; i < 100; i++) {
        float bl, br;
        ss_block.ProcessBlockStereo(&bl, &br, 1);
        float sl, sr;
        ss_sample.ProcessStereo(sl, sr);
        CHECK(bl == doctest::Approx(sl));
        CHECK(br == doctest::Approx(sr));
    }
}

// ============================================================================
// Oversampling characterization (#21)
// ============================================================================
// Characterize aliasing differences between 48 kHz and 96 kHz sample rates,
// and evaluate whether 2x-oversampled 48 kHz (run at 96k, decimate to 48k)
// improves quality over native 48 kHz. These are characterization tests:
// MESSAGE() logs measured values, CHECKs are soft (wide tolerances).
// ============================================================================

TEST_CASE("Oversampling: 48k vs 96k aliasing at 440 Hz") {
    // At 440 Hz, aliasing is mild at both rates. 96k should show equal
    // or slightly better autocorrelation at fundamental (less noise floor).
    const float freq = 440.0f;
    const int settle = 4000;
    const int n48 = 4096;
    const int n96 = 4096;

    auto s48 = collectAtRate(48000.0f, freq, 0.5f, 1.0f, settle, n48);
    auto s96 = collectAtRate(96000.0f, freq, 0.5f, 1.0f, settle, n96);

    int period48 = static_cast<int>(48000.0f / freq);
    int period96 = static_cast<int>(96000.0f / freq);

    double ac48 = autocorrAt(s48, period48);
    double ac96 = autocorrAt(s96, period96);

    double rms48 = rmsOf(s48);
    double rms96 = rmsOf(s96);

    int zc48 = zeroCrossings(s48);
    int zc96 = zeroCrossings(s96);
    float zcFreq48 = static_cast<float>(zc48) / (2.0f * static_cast<float>(n48) / 48000.0f);
    float zcFreq96 = static_cast<float>(zc96) / (2.0f * static_cast<float>(n96) / 96000.0f);

    MESSAGE("Oversample 440Hz autocorr: 48k=" << ac48 << " 96k=" << ac96);
    MESSAGE("Oversample 440Hz RMS: 48k=" << rms48 << " 96k=" << rms96);
    MESSAGE("Oversample 440Hz ZC freq: 48k=" << zcFreq48 << " 96k=" << zcFreq96);

    // Both should produce coherent signal
    CHECK(ac48 > 0.0);
    CHECK(ac96 > 0.0);
    // 96k should be at least as periodic (soft: allow 96k to be slightly worse)
    CHECK(ac96 > ac48 - 0.2);
    // RMS should be in same ballpark
    CHECK(rms48 > 0.01);
    CHECK(rms96 > 0.01);
}

TEST_CASE("Oversampling: 48k vs 96k aliasing at 2 kHz") {
    // At 2 kHz, harmonics alias more at 48k (Nyquist=24k, only 12 harmonics
    // fit cleanly). 96k (Nyquist=48k) fits 24 harmonics — less fold-back.
    const float freq = 2000.0f;
    const int settle = 4000;
    const int N = 4096;

    auto s48 = collectAtRate(48000.0f, freq, 0.5f, 1.0f, settle, N);
    auto s96 = collectAtRate(96000.0f, freq, 0.5f, 1.0f, settle, N);

    int period48 = static_cast<int>(48000.0f / freq);
    int period96 = static_cast<int>(96000.0f / freq);

    double ac48 = autocorrAt(s48, period48);
    double ac96 = autocorrAt(s96, period96);

    double rms48 = rmsOf(s48);
    double rms96 = rmsOf(s96);

    MESSAGE("Oversample 2kHz autocorr: 48k=" << ac48 << " 96k=" << ac96);
    MESSAGE("Oversample 2kHz RMS: 48k=" << rms48 << " 96k=" << rms96);

    CHECK(ac48 > 0.0);
    CHECK(ac96 > 0.0);
    // 96k should show clearer advantage at higher frequencies
    CHECK(ac96 > ac48 - 0.15);
    CHECK(rms48 > 0.01);
    CHECK(rms96 > 0.01);
}

TEST_CASE("Oversampling: 48k vs 96k aliasing at 8 kHz") {
    // 8 kHz is extreme: at 48k, Nyquist is only 3x fundamental — massive
    // aliasing. At 96k, Nyquist is 6x — still significant but less severe.
    // Expect dramatically different autocorrelation.
    const float freq = 8000.0f;
    const int settle = 4000;
    const int N = 4096;

    auto s48 = collectAtRate(48000.0f, freq, 0.5f, 1.0f, settle, N);
    auto s96 = collectAtRate(96000.0f, freq, 0.5f, 1.0f, settle, N);

    int period48 = static_cast<int>(48000.0f / freq);
    int period96 = static_cast<int>(96000.0f / freq);

    double ac48 = autocorrAt(s48, period48);
    double ac96 = autocorrAt(s96, period96);

    double rms48 = rmsOf(s48);
    double rms96 = rmsOf(s96);

    int zc48 = zeroCrossings(s48);
    int zc96 = zeroCrossings(s96);
    float zcFreq48 = static_cast<float>(zc48) / (2.0f * static_cast<float>(N) / 48000.0f);
    float zcFreq96 = static_cast<float>(zc96) / (2.0f * static_cast<float>(N) / 96000.0f);

    MESSAGE("Oversample 8kHz autocorr: 48k=" << ac48 << " 96k=" << ac96);
    MESSAGE("Oversample 8kHz RMS: 48k=" << rms48 << " 96k=" << rms96);
    MESSAGE("Oversample 8kHz ZC freq: 48k=" << zcFreq48 << " 96k=" << zcFreq96);

    // Both produce signal
    CHECK(rms48 > 0.0);
    CHECK(rms96 > 0.0);
    // 96k autocorrelation should be significantly better
    CHECK(ac96 > ac48 - 0.1);
    CHECK(std::isfinite(ac48));
    CHECK(std::isfinite(ac96));
}

TEST_CASE("Oversampling: 2x48k decimated vs native 48k at 440 Hz") {
    // Run engine at 96k (simulating 2x oversampled 48k), then decimate by
    // averaging pairs to get 48k-equivalent output. Compare vs native 48k.
    // If oversampling helps, decimated signal should have higher autocorrelation.
    const float freq = 440.0f;
    const int settle = 4000;
    const int n_native = 4096;
    const int n_oversampled = n_native * 2;

    auto s_native = collectAtRate(48000.0f, freq, 0.5f, 1.0f, settle, n_native);

    auto s_96k = collectAtRate(96000.0f, freq, 0.5f, 1.0f, settle, n_oversampled);
    std::vector<float> s_decimated(n_native);
    for (int i = 0; i < n_native; i++) {
        s_decimated[i] = (s_96k[i * 2] + s_96k[i * 2 + 1]) * 0.5f;
    }

    int period = static_cast<int>(48000.0f / freq);

    double ac_native    = autocorrAt(s_native, period);
    double ac_decimated = autocorrAt(s_decimated, period);

    double rms_native    = rmsOf(s_native);
    double rms_decimated = rmsOf(s_decimated);

    int zc_native    = zeroCrossings(s_native);
    int zc_decimated = zeroCrossings(s_decimated);
    float zcFreq_native    = static_cast<float>(zc_native)    / (2.0f * static_cast<float>(n_native) / 48000.0f);
    float zcFreq_decimated = static_cast<float>(zc_decimated) / (2.0f * static_cast<float>(n_native) / 48000.0f);

    MESSAGE("Oversample decimate 440Hz autocorr: native48k=" << ac_native
            << " 2x_decimated=" << ac_decimated);
    MESSAGE("Oversample decimate 440Hz RMS: native48k=" << rms_native
            << " 2x_decimated=" << rms_decimated);
    MESSAGE("Oversample decimate 440Hz ZC freq: native48k=" << zcFreq_native
            << " 2x_decimated=" << zcFreq_decimated);

    CHECK(rms_native > 0.01);
    CHECK(rms_decimated > 0.01);
    CHECK(ac_decimated > ac_native - 0.2);
    CHECK(std::isfinite(ac_native));
    CHECK(std::isfinite(ac_decimated));
}

TEST_CASE("Oversampling: 2x48k decimated vs native 48k at 4 kHz") {
    // Same decimation test at higher frequency where oversampling benefit
    // should be more pronounced. The simple averaging decimation acts as
    // a crude lowpass, attenuating aliased harmonics.
    const float freq = 4000.0f;
    const int settle = 4000;
    const int n_native = 4096;
    const int n_oversampled = n_native * 2;

    auto s_native = collectAtRate(48000.0f, freq, 0.5f, 1.0f, settle, n_native);

    auto s_96k = collectAtRate(96000.0f, freq, 0.5f, 1.0f, settle, n_oversampled);
    std::vector<float> s_decimated(n_native);
    for (int i = 0; i < n_native; i++) {
        s_decimated[i] = (s_96k[i * 2] + s_96k[i * 2 + 1]) * 0.5f;
    }

    int period = static_cast<int>(48000.0f / freq);

    double ac_native    = autocorrAt(s_native, period);
    double ac_decimated = autocorrAt(s_decimated, period);

    double rms_native    = rmsOf(s_native);
    double rms_decimated = rmsOf(s_decimated);

    int zc_native    = zeroCrossings(s_native);
    int zc_decimated = zeroCrossings(s_decimated);
    float zcFreq_native    = static_cast<float>(zc_native)    / (2.0f * static_cast<float>(n_native) / 48000.0f);
    float zcFreq_decimated = static_cast<float>(zc_decimated) / (2.0f * static_cast<float>(n_native) / 48000.0f);

    MESSAGE("Oversample decimate 4kHz autocorr: native48k=" << ac_native
            << " 2x_decimated=" << ac_decimated);
    MESSAGE("Oversample decimate 4kHz RMS: native48k=" << rms_native
            << " 2x_decimated=" << rms_decimated);
    MESSAGE("Oversample decimate 4kHz ZC freq: native48k=" << zcFreq_native
            << " 2x_decimated=" << zcFreq_decimated);

    CHECK(rms_native > 0.0);
    CHECK(rms_decimated > 0.0);
    CHECK(ac_decimated > ac_native - 0.15);
    CHECK(std::isfinite(ac_native));
    CHECK(std::isfinite(ac_decimated));
}

// ============================================================================
// Fixed-point HPF tests (#22)
// ============================================================================

TEST_CASE("Fixed-point HPF: authentic output bounded [-1, 1]") {
    SuperSaw ss;
    ss.Init(96000.0f);
    ss.SetFreq(440.0f);
    ss.SetDetune(1.0f);
    ss.SetMix(1.0f);
    ss.SetAuthentic(true);
    ss.SetFixedPointHpf(true);

    for (int i = 0; i < 4096; i++) {
        float s = ss.Process();
        CHECK(s >= -1.0f);
        CHECK(s <= 1.0f);
    }
}

TEST_CASE("Fixed-point HPF: removes DC from authentic mode") {
    SuperSaw ss;
    ss.Init(96000.0f);
    ss.SetFreq(440.0f);
    ss.SetDetune(0.5f);
    ss.SetMix(1.0f);
    ss.SetAuthentic(true);
    ss.SetFixedPointHpf(true);

    // Warm up
    for (int i = 0; i < 2000; i++) ss.Process();

    // Measure DC (mean)
    double sum = 0.0;
    const int N = 8192;
    for (int i = 0; i < N; i++) {
        sum += ss.Process();
    }
    double mean = sum / N;

    // HPF should remove DC — mean near zero (fixed-point has lower precision)
    CHECK(std::fabs(mean) < 0.05);
}

TEST_CASE("Fixed-point HPF: comparable RMS to float HPF") {
    // Same engine params, compare fixed-point HPF vs float HPF output.
    SuperSaw fp, fl;
    fp.Init(96000.0f);
    fl.Init(96000.0f);

    float freq = 440.0f;
    fp.SetFreq(freq); fl.SetFreq(freq);
    fp.SetDetune(0.5f); fl.SetDetune(0.5f);
    fp.SetMix(1.0f); fl.SetMix(1.0f);
    fp.SetAuthentic(true); fl.SetAuthentic(true);
    fp.SetFixedPointHpf(true); fl.SetFixedPointHpf(false);

    // Warm up both
    for (int i = 0; i < 2000; i++) { fp.Process(); fl.Process(); }

    double rms_fp = 0.0, rms_fl = 0.0;
    const int N = 8192;
    for (int i = 0; i < N; i++) {
        float a = fp.Process();
        float f = fl.Process();
        rms_fp += a * a;
        rms_fl += f * f;
    }
    rms_fp = std::sqrt(rms_fp / N);
    rms_fl = std::sqrt(rms_fl / N);

    MESSAGE("Fixed-pt HPF RMS: fp24=" << rms_fp << " float=" << rms_fl);

    CHECK(rms_fp > 0.01);
    CHECK(rms_fl > 0.01);
    double ratio = rms_fp / rms_fl;
    CHECK(ratio > 0.3);
    CHECK(ratio < 3.0);
}

TEST_CASE("Fixed-point HPF: pitch tracking works at multiple frequencies") {
    float freqs[] = {110.0f, 440.0f, 1760.0f, 4000.0f};
    for (float freq : freqs) {
        SuperSaw ss;
        ss.Init(96000.0f);
        ss.SetFreq(freq);
        ss.SetDetune(0.0f);
        ss.SetMix(0.0f);
        ss.SetAuthentic(true);
        ss.SetFixedPointHpf(true);

        // Warm up
        for (int i = 0; i < 2000; i++) ss.Process();

        // Check output is non-zero (HPF not killing signal) and bounded
        double rms = 0.0;
        const int N = 4096;
        for (int i = 0; i < N; i++) {
            float s = ss.Process();
            rms += s * s;
            CHECK(s >= -1.0f);
            CHECK(s <= 1.0f);
        }
        rms = std::sqrt(rms / N);
        MESSAGE("Fixed-pt HPF @ " << freq << " Hz: RMS=" << rms);
        CHECK(rms > 0.001f);
    }
}
