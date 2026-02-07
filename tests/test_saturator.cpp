#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "saturator.h"
#include <cmath>

// ============================================================================
// Saturator: tanh-based soft clipping with gain compensation
// ============================================================================

TEST_CASE("Saturator: drive=0 is unity bypass") {
    Saturator sat;
    sat.Init();

    float values[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.25f, -0.75f};
    for (float v : values) {
        CHECK(sat.Process(v) == doctest::Approx(v));
    }
}

TEST_CASE("Saturator: output is odd-symmetric f(-x) == -f(x)") {
    Saturator sat;
    sat.Init();

    float drives[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    float inputs[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f};

    for (float d : drives) {
        sat.SetDrive(d);
        for (float x : inputs) {
            float pos = sat.Process(x);
            float neg = sat.Process(-x);
            CHECK(pos == doctest::Approx(-neg).epsilon(1e-6));
        }
    }
}

TEST_CASE("Saturator: output bounded to [-1, 1] for input in [-1, 1]") {
    Saturator sat;
    sat.Init();

    float drives[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (float d : drives) {
        sat.SetDrive(d);
        for (int i = -100; i <= 100; i++) {
            float input = i / 100.0f;
            float out = sat.Process(input);
            CHECK(out >= -1.0f);
            CHECK(out <= 1.0f);
        }
    }
}

TEST_CASE("Saturator: gain compensation keeps output=1 at input=1") {
    Saturator sat;
    sat.Init();

    // Gain compensation normalizes so tanh(scaled*1)/tanh(scaled) = 1.0
    float drives[] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (float d : drives) {
        sat.SetDrive(d);
        float out = sat.Process(1.0f);
        CHECK(out == doctest::Approx(1.0f).epsilon(1e-5));
    }
}

TEST_CASE("Saturator: monotonic - increasing input gives increasing output") {
    Saturator sat;
    sat.Init();

    float drives[] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (float d : drives) {
        sat.SetDrive(d);
        float prev = sat.Process(-1.0f);
        for (int i = -99; i <= 100; i++) {
            float input = i / 100.0f;
            float out = sat.Process(input);
            CHECK(out >= prev);
            prev = out;
        }
    }
}

TEST_CASE("Saturator: higher drive flattens the curve more at mid amplitude") {
    Saturator sat;
    sat.Init();

    // With gain compensation, tanh saturation compresses peaks.
    // At mid-range (0.5), higher drive squashes dynamic range more:
    // the ratio output/input at 0.5 should increase with drive (boost toward 1.0),
    // demonstrating more compression (less difference between soft and loud).
    float input = 0.5f;
    float prev_ratio = 1.0f;  // drive=0: ratio=1.0 (bypass)

    float drives[] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (float d : drives) {
        sat.SetDrive(d);
        float out = sat.Process(input);
        float ratio = out / input;
        CHECK(ratio > prev_ratio);
        prev_ratio = ratio;
    }
}

TEST_CASE("Saturator: zero input always produces zero output") {
    Saturator sat;
    sat.Init();

    float drives[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (float d : drives) {
        sat.SetDrive(d);
        CHECK(sat.Process(0.0f) == doctest::Approx(0.0f));
    }
}

TEST_CASE("Saturator: Init resets drive to bypass") {
    Saturator sat;
    sat.Init();
    sat.SetDrive(1.0f);

    // Re-init should reset to bypass
    sat.Init();
    float out = sat.Process(0.9f);
    CHECK(out == doctest::Approx(0.9f));
}
