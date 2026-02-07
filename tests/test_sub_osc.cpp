#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "sub_osc.h"
#include <cmath>

// ============================================================================
// SubOsc: -1 octave square wave sub-oscillator
// ============================================================================

static const float kSampleRate = 96000.0f;

TEST_CASE("SubOsc: SetFreq(440) produces 220 Hz output") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(440.0f);
    sub.SetLevel(1.0f);

    // 220 Hz sub at 96 kHz. Run many samples and measure frequency
    // from zero-crossing rate.
    const int num_samples = 96000;  // 1 second
    int crossings = 0;
    float prev = sub.Process();
    for (int i = 1; i < num_samples; i++) {
        float cur = sub.Process();
        if ((prev > 0.0f && cur < 0.0f) || (prev < 0.0f && cur > 0.0f)) {
            crossings++;
        }
        prev = cur;
    }

    // 2 crossings per period, 220 periods/sec = 440 crossings
    // Allow +/-2 for boundary effects
    CHECK(crossings >= 438);
    CHECK(crossings <= 442);
}

TEST_CASE("SubOsc: square wave output is only +level or -level") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(440.0f);
    sub.SetLevel(0.7f);

    for (int i = 0; i < 1000; i++) {
        float out = sub.Process();
        bool valid = (out == doctest::Approx(0.7f)) ||
                     (out == doctest::Approx(-0.7f));
        CHECK(valid);
    }
}

TEST_CASE("SubOsc: level 0 produces silence") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(440.0f);
    sub.SetLevel(0.0f);

    for (int i = 0; i < 1000; i++) {
        CHECK(sub.Process() == doctest::Approx(0.0f));
    }
}

TEST_CASE("SubOsc: level scaling matches parameter") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(440.0f);

    float levels[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (float lvl : levels) {
        sub.Init(kSampleRate);
        sub.SetFreq(440.0f);
        sub.SetLevel(lvl);

        float out = sub.Process();
        float mag = std::fabs(out);
        CHECK(mag == doctest::Approx(lvl));
    }
}

TEST_CASE("SubOsc: frequency tracking - SetFreq(880) gives 440 Hz") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(880.0f);
    sub.SetLevel(1.0f);

    // 440 Hz sub at 96 kHz. Run 1 second, count crossings.
    const int num_samples = 96000;
    int crossings = 0;
    float prev = sub.Process();
    for (int i = 1; i < num_samples; i++) {
        float cur = sub.Process();
        if ((prev > 0.0f && cur < 0.0f) || (prev < 0.0f && cur > 0.0f)) {
            crossings++;
        }
        prev = cur;
    }

    // 2 crossings per period, 440 periods/sec = 880 crossings +/-2
    CHECK(crossings >= 878);
    CHECK(crossings <= 882);
}

TEST_CASE("SubOsc: frequency tracking - SetFreq(220) gives 110 Hz") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(220.0f);
    sub.SetLevel(1.0f);

    // 110 Hz sub at 96 kHz. Run 1 second, count crossings.
    const int num_samples = 96000;
    int crossings = 0;
    float prev = sub.Process();
    for (int i = 1; i < num_samples; i++) {
        float cur = sub.Process();
        if ((prev > 0.0f && cur < 0.0f) || (prev < 0.0f && cur > 0.0f)) {
            crossings++;
        }
        prev = cur;
    }

    // 2 crossings per period, 110 periods/sec = 220 crossings +/-2
    CHECK(crossings >= 218);
    CHECK(crossings <= 222);
}

TEST_CASE("SubOsc: approximately 50% duty cycle") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(480.0f);  // 240 Hz sub, period = 400 samples
    sub.SetLevel(1.0f);

    // Run for many periods and verify close to 50/50 split
    const int num_samples = 96000;
    int positive = 0;
    for (int i = 0; i < num_samples; i++) {
        float out = sub.Process();
        if (out > 0.0f) positive++;
    }

    // Allow up to 0.1% deviation from perfect 50/50
    float ratio = static_cast<float>(positive) / num_samples;
    CHECK(ratio == doctest::Approx(0.5f).epsilon(0.001));
}

TEST_CASE("SubOsc: phase wrapping - no discontinuities after many samples") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(440.0f);
    sub.SetLevel(1.0f);

    // Run for a large number of samples, check output stays bounded
    for (int i = 0; i < 100000; i++) {
        float out = sub.Process();
        CHECK(out >= -1.0f);
        CHECK(out <= 1.0f);
    }
}

TEST_CASE("SubOsc: Init resets phase") {
    SubOsc sub;
    sub.Init(kSampleRate);
    sub.SetFreq(440.0f);
    sub.SetLevel(1.0f);

    // Advance phase partway
    for (int i = 0; i < 500; i++) sub.Process();

    // Re-init should reset to start
    sub.Init(kSampleRate);
    sub.SetFreq(440.0f);
    sub.SetLevel(1.0f);

    // First sample after init: phase=0 < 0.5, should be +1
    float out = sub.Process();
    CHECK(out == doctest::Approx(1.0f));
}
