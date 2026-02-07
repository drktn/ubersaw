#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "envelope.h"
#include <cmath>

// ============================================================================
// Envelope: attack/release envelope follower (issue #19)
// ============================================================================

static const float kSampleRate = 96000.0f;

TEST_CASE("Envelope: Init resets value to 0") {
    Envelope env;
    env.Init(kSampleRate);
    CHECK(env.GetValue() == doctest::Approx(0.0f));
}

TEST_CASE("Envelope: gate on ramps from 0 toward 1") {
    Envelope env;
    env.Init(kSampleRate);
    env.SetAttack(0.01f);
    env.Gate(true);

    float prev = 0.0f;
    for (int i = 0; i < 1000; i++) {
        float out = env.Process();
        CHECK(out >= prev);
        prev = out;
    }
    CHECK(prev > 0.0f);
}

TEST_CASE("Envelope: gate off ramps from 1 toward 0") {
    Envelope env;
    env.Init(kSampleRate);
    env.SetAttack(0.001f);
    env.Gate(true);

    // Ramp up to near 1
    for (int i = 0; i < 48000; i++) env.Process();
    CHECK(env.GetValue() > 0.99f);

    // Gate off: should decay
    env.Gate(false);
    env.SetRelease(0.01f);
    float prev = env.GetValue();
    for (int i = 0; i < 1000; i++) {
        float out = env.Process();
        CHECK(out <= prev);
        prev = out;
    }
    CHECK(prev < 0.99f);
}

TEST_CASE("Envelope: output always bounded [0, 1]") {
    Envelope env;
    env.Init(kSampleRate);
    env.SetAttack(0.005f);
    env.SetRelease(0.01f);

    // Gate on for a while, then off
    env.Gate(true);
    for (int i = 0; i < 96000; i++) {
        float out = env.Process();
        CHECK(out >= 0.0f);
        CHECK(out <= 1.0f);
    }
    env.Gate(false);
    for (int i = 0; i < 96000; i++) {
        float out = env.Process();
        CHECK(out >= 0.0f);
        CHECK(out <= 1.0f);
    }
}

TEST_CASE("Envelope: faster attack reaches 0.63 sooner") {
    // One time constant: ~63.2% of target
    const float target = 0.632f;

    // Fast attack
    Envelope fast;
    fast.Init(kSampleRate);
    fast.SetAttack(0.005f);
    fast.Gate(true);

    int fast_samples = 0;
    for (int i = 0; i < 96000; i++) {
        fast.Process();
        fast_samples++;
        if (fast.GetValue() >= target) break;
    }

    // Slow attack
    Envelope slow;
    slow.Init(kSampleRate);
    slow.SetAttack(0.05f);
    slow.Gate(true);

    int slow_samples = 0;
    for (int i = 0; i < 96000; i++) {
        slow.Process();
        slow_samples++;
        if (slow.GetValue() >= target) break;
    }

    CHECK(fast_samples < slow_samples);
}

TEST_CASE("Envelope: faster release decays to 0.37 sooner") {
    const float target = 0.368f;  // 1 - 0.632

    // Fast release
    Envelope fast;
    fast.Init(kSampleRate);
    fast.SetAttack(0.001f);
    fast.Gate(true);
    for (int i = 0; i < 48000; i++) fast.Process();
    fast.Gate(false);
    fast.SetRelease(0.005f);

    int fast_samples = 0;
    for (int i = 0; i < 96000; i++) {
        fast.Process();
        fast_samples++;
        if (fast.GetValue() <= target) break;
    }

    // Slow release
    Envelope slow;
    slow.Init(kSampleRate);
    slow.SetAttack(0.001f);
    slow.Gate(true);
    for (int i = 0; i < 48000; i++) slow.Process();
    slow.Gate(false);
    slow.SetRelease(0.05f);

    int slow_samples = 0;
    for (int i = 0; i < 96000; i++) {
        slow.Process();
        slow_samples++;
        if (slow.GetValue() <= target) break;
    }

    CHECK(fast_samples < slow_samples);
}

TEST_CASE("Envelope: stays near 0 with gate off for many samples") {
    Envelope env;
    env.Init(kSampleRate);
    env.Gate(false);

    for (int i = 0; i < 96000; i++) env.Process();
    CHECK(env.GetValue() == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("Envelope: stays near 1 with gate on for many samples") {
    Envelope env;
    env.Init(kSampleRate);
    env.SetAttack(0.01f);
    env.Gate(true);

    for (int i = 0; i < 96000; i++) env.Process();
    CHECK(env.GetValue() == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("Envelope: GetValue returns current envelope level") {
    Envelope env;
    env.Init(kSampleRate);
    env.SetAttack(0.001f);
    env.Gate(true);

    float out = env.Process();
    CHECK(env.GetValue() == doctest::Approx(out));

    // Process more and check consistency
    for (int i = 0; i < 100; i++) out = env.Process();
    CHECK(env.GetValue() == doctest::Approx(out));
}

TEST_CASE("Envelope: attack time constant approximates specified seconds") {
    float attack_sec = 0.01f;
    Envelope env;
    env.Init(kSampleRate);
    env.SetAttack(attack_sec);
    env.Gate(true);

    // One time constant should reach ~63.2%
    int expected_samples = static_cast<int>(attack_sec * kSampleRate);
    for (int i = 0; i < expected_samples; i++) env.Process();

    CHECK(env.GetValue() == doctest::Approx(0.632f).epsilon(0.05));
}

TEST_CASE("Envelope: minimum time clamp prevents zero-length envelope") {
    Envelope env;
    env.Init(kSampleRate);
    env.SetAttack(0.0f);   // should clamp to 0.001
    env.SetRelease(0.0f);  // should clamp to 0.001
    env.Gate(true);

    // Should still ramp (not instant jump)
    float first = env.Process();
    CHECK(first < 1.0f);
    CHECK(first > 0.0f);
}
