#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "lfo.h"
#include <cmath>

// ============================================================================
// Lfo: internal LFO with sine, triangle, square, sample-and-hold
// ============================================================================

static const float kSampleRate = 48000.0f;

// --- Sine ---

TEST_CASE("Lfo: sine output bounded [-1, 1]") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SINE);
    lfo.SetFreq(5.0f);

    for (int i = 0; i < 96000; i++) {
        float out = lfo.Process();
        CHECK(out >= -1.0f);
        CHECK(out <= 1.0f);
    }
}

TEST_CASE("Lfo: sine correct frequency via zero-crossings") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SINE);
    float freq = 10.0f;
    lfo.SetFreq(freq);

    int zero_crossings = 0;
    float prev = lfo.Process();
    int num_samples = static_cast<int>(kSampleRate);  // 1 second
    for (int i = 1; i < num_samples; i++) {
        float out = lfo.Process();
        if ((prev >= 0.0f && out < 0.0f) || (prev < 0.0f && out >= 0.0f)) {
            zero_crossings++;
        }
        prev = out;
    }
    // 2 zero-crossings per cycle
    float measured_freq = zero_crossings / 2.0f;
    CHECK(measured_freq == doctest::Approx(freq).epsilon(0.05));
}

TEST_CASE("Lfo: sine starts at 0 after Init") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SINE);
    lfo.SetFreq(1.0f);

    float out = lfo.Process();
    CHECK(out == doctest::Approx(0.0f).epsilon(0.01));
}

// --- Triangle ---

TEST_CASE("Lfo: triangle output bounded [-1, 1]") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::TRIANGLE);
    lfo.SetFreq(5.0f);

    for (int i = 0; i < 96000; i++) {
        float out = lfo.Process();
        CHECK(out >= -1.0f);
        CHECK(out <= 1.0f);
    }
}

TEST_CASE("Lfo: triangle correct frequency via zero-crossings") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::TRIANGLE);
    float freq = 10.0f;
    lfo.SetFreq(freq);

    int zero_crossings = 0;
    float prev = lfo.Process();
    int num_samples = static_cast<int>(kSampleRate);
    for (int i = 1; i < num_samples; i++) {
        float out = lfo.Process();
        if ((prev >= 0.0f && out < 0.0f) || (prev < 0.0f && out >= 0.0f)) {
            zero_crossings++;
        }
        prev = out;
    }
    float measured_freq = zero_crossings / 2.0f;
    CHECK(measured_freq == doctest::Approx(freq).epsilon(0.05));
}

TEST_CASE("Lfo: triangle peaks at +1 and -1") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::TRIANGLE);
    lfo.SetFreq(1.0f);

    float max_val = -2.0f;
    float min_val = 2.0f;
    int num_samples = static_cast<int>(kSampleRate);  // 1 full cycle
    for (int i = 0; i < num_samples; i++) {
        float out = lfo.Process();
        if (out > max_val) max_val = out;
        if (out < min_val) min_val = out;
    }
    CHECK(max_val == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(min_val == doctest::Approx(-1.0f).epsilon(0.01));
}

// --- Square ---

TEST_CASE("Lfo: square only outputs +1 or -1") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SQUARE);
    lfo.SetFreq(5.0f);

    for (int i = 0; i < 96000; i++) {
        float out = lfo.Process();
        CHECK((out == doctest::Approx(1.0f) || out == doctest::Approx(-1.0f)));
    }
}

TEST_CASE("Lfo: square 50% duty cycle") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SQUARE);
    lfo.SetFreq(10.0f);

    int high_count = 0;
    int total = static_cast<int>(kSampleRate);  // 1 second = 10 full cycles
    for (int i = 0; i < total; i++) {
        float out = lfo.Process();
        if (out > 0.0f) high_count++;
    }
    float ratio = static_cast<float>(high_count) / total;
    CHECK(ratio == doctest::Approx(0.5f).epsilon(0.01));
}

// --- Sample and Hold ---

TEST_CASE("Lfo: S&H output bounded [-1, 1]") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SAMPLE_AND_HOLD);
    lfo.SetFreq(10.0f);

    for (int i = 0; i < 96000; i++) {
        float out = lfo.Process();
        CHECK(out >= -1.0f);
        CHECK(out <= 1.0f);
    }
}

TEST_CASE("Lfo: S&H holds value between phase resets") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SAMPLE_AND_HOLD);
    lfo.SetFreq(1.0f);  // 1 Hz = 48000 samples per cycle

    // Process first sample to get initial value
    float first = lfo.Process();

    // All subsequent samples within the same cycle should hold the same value
    // Check first 1000 samples (well within one 48000-sample cycle)
    for (int i = 0; i < 1000; i++) {
        float out = lfo.Process();
        CHECK(out == doctest::Approx(first));
    }
}

TEST_CASE("Lfo: S&H changes value at phase reset") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SAMPLE_AND_HOLD);
    lfo.SetFreq(10.0f);  // 10 Hz = 4800 samples per cycle

    // Collect values at each cycle boundary
    int changes = 0;
    float prev = lfo.Process();
    // Run for 10 cycles
    for (int i = 1; i < 48000; i++) {
        float out = lfo.Process();
        if (out != prev) {
            changes++;
            prev = out;
        }
    }
    // Should have ~9 value changes (10 cycles, first value set at init)
    CHECK(changes >= 8);
    CHECK(changes <= 11);
}

// --- Reset ---

TEST_CASE("Lfo: Reset resets phase to 0") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SINE);
    lfo.SetFreq(1.0f);

    // Advance partway through a cycle
    for (int i = 0; i < 12000; i++) lfo.Process();

    // Reset and check that output is back to sin(0) = 0
    lfo.Reset();
    float out = lfo.Process();
    CHECK(out == doctest::Approx(0.0f).epsilon(0.01));
}

// --- SetFreq ---

TEST_CASE("Lfo: SetFreq changes period") {
    Lfo lfo;
    lfo.Init(kSampleRate);
    lfo.SetWaveform(Lfo::SQUARE);

    // Measure period at 10 Hz
    lfo.SetFreq(10.0f);
    int crossings_10hz = 0;
    float prev = lfo.Process();
    for (int i = 1; i < 48000; i++) {
        float out = lfo.Process();
        if ((prev > 0.0f && out < 0.0f) || (prev < 0.0f && out > 0.0f)) {
            crossings_10hz++;
        }
        prev = out;
    }

    // Measure period at 20 Hz
    lfo.Reset();
    lfo.SetFreq(20.0f);
    int crossings_20hz = 0;
    prev = lfo.Process();
    for (int i = 1; i < 48000; i++) {
        float out = lfo.Process();
        if ((prev > 0.0f && out < 0.0f) || (prev < 0.0f && out > 0.0f)) {
            crossings_20hz++;
        }
        prev = out;
    }

    // 20 Hz should have ~2x the crossings of 10 Hz
    CHECK(crossings_20hz == doctest::Approx(crossings_10hz * 2).epsilon(2));
}
