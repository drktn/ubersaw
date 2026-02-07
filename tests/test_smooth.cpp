#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "smooth.h"
#include <cmath>
#include <vector>

// ============================================================================
// Smooth: one-pole lowpass for ADC parameter smoothing
// ============================================================================

TEST_CASE("Smooth: coeff=0 passes input through instantly") {
    Smooth s;
    s.Init(0.0f);
    CHECK(s.Process(1.0f) == doctest::Approx(1.0f));
    CHECK(s.Process(0.5f) == doctest::Approx(0.5f));
    CHECK(s.Process(0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("Smooth: coeff=0.99 changes very slowly") {
    Smooth s;
    s.Init(0.99f);

    // After one sample of 1.0, output should be 0.01 (barely moved)
    float out = s.Process(1.0f);
    CHECK(out == doctest::Approx(0.01f).epsilon(0.001));

    // After 100 more samples of 1.0, still well below target
    for (int i = 0; i < 99; i++) out = s.Process(1.0f);
    CHECK(out < 0.7f);
}

TEST_CASE("Smooth: step response converges to target") {
    Smooth s;
    s.Init(0.9f);

    // Feed 1.0 for 1000 iterations, should converge very close to 1.0
    float out = 0.0f;
    for (int i = 0; i < 1000; i++) out = s.Process(1.0f);
    CHECK(out == doctest::Approx(1.0f).epsilon(0.0001));
}

TEST_CASE("Smooth: step response is monotonically increasing for step up") {
    Smooth s;
    s.Init(0.9f);

    float prev = 0.0f;
    for (int i = 0; i < 100; i++) {
        float out = s.Process(1.0f);
        CHECK(out >= prev);
        prev = out;
    }
}

TEST_CASE("Smooth: SetValue forces immediate value") {
    Smooth s;
    s.Init(0.99f);
    s.SetValue(0.75f);

    // Next process with same value should return ~0.75
    float out = s.Process(0.75f);
    CHECK(out == doctest::Approx(0.75f).epsilon(0.01));
}

TEST_CASE("Smooth: SetCoeff changes smoothing on the fly") {
    Smooth s;
    s.Init(0.99f);  // very slow

    s.Process(1.0f);
    float slow = s.Process(1.0f);

    // Reset and use fast coeff
    s.SetValue(0.0f);
    s.SetCoeff(0.0f);  // instant
    float fast = s.Process(1.0f);

    CHECK(fast > slow);  // fast should jump ahead
    CHECK(fast == doctest::Approx(1.0f));
}

TEST_CASE("Smooth: noise reduction - noisy input produces smoother output") {
    Smooth s;
    s.Init(0.95f);

    // Generate noisy signal around 0.5
    // Simple deterministic noise using integer math
    float input_var = 0.0f;
    float output_var = 0.0f;
    float out = 0.5f;
    s.SetValue(0.5f);

    const int N = 1000;
    for (int i = 0; i < N; i++) {
        // Deterministic "noise" around 0.5
        float noise = 0.5f + 0.1f * ((i * 7 + 3) % 11 - 5) / 5.0f;
        input_var += (noise - 0.5f) * (noise - 0.5f);

        out = s.Process(noise);
        output_var += (out - 0.5f) * (out - 0.5f);
    }

    input_var /= N;
    output_var /= N;

    // Output variance should be significantly less than input variance
    CHECK(output_var < input_var * 0.5f);
}
