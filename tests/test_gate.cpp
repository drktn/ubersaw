#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "gate_output.h"

// ============================================================================
// GateOutput: pass-through and clock divider for gate signal
// ============================================================================

TEST_CASE("GateOutput: pass-through mirrors input") {
    GateOutput g;
    g.Init();
    g.SetMode(GateOutput::PASSTHROUGH);

    CHECK(g.Process(false) == false);
    CHECK(g.Process(true) == true);
    CHECK(g.Process(true) == true);
    CHECK(g.Process(false) == false);
    CHECK(g.Process(true) == true);
}

TEST_CASE("GateOutput: clock div 2 toggles every 2 rising edges") {
    GateOutput g;
    g.Init();
    g.SetMode(GateOutput::CLOCK_DIV);
    g.SetDivisor(2);

    // Initial output should be false
    CHECK(g.Process(false) == false);

    // Rising edge 1 — no toggle yet
    CHECK(g.Process(true) == false);
    CHECK(g.Process(false) == false);  // gate low between pulses

    // Rising edge 2 — toggle on
    CHECK(g.Process(true) == true);
    CHECK(g.Process(false) == true);   // stays high until next toggle

    // Rising edge 3 — no toggle
    CHECK(g.Process(true) == true);
    CHECK(g.Process(false) == true);

    // Rising edge 4 — toggle off
    CHECK(g.Process(true) == false);
    CHECK(g.Process(false) == false);
}

TEST_CASE("GateOutput: clock div 4 toggles every 4 rising edges") {
    GateOutput g;
    g.Init();
    g.SetMode(GateOutput::CLOCK_DIV);
    g.SetDivisor(4);

    // Send 4 gate pulses (rising + falling each)
    // Pulse 1
    g.Process(true); g.Process(false);
    // Pulse 2
    g.Process(true); g.Process(false);
    // Pulse 3
    g.Process(true); g.Process(false);
    // Pulse 4 — should toggle on
    bool out = g.Process(true);
    CHECK(out == true);

    // Stays high through next 3 pulses
    g.Process(false);
    g.Process(true); g.Process(false);  // pulse 5
    g.Process(true); g.Process(false);  // pulse 6
    g.Process(true); g.Process(false);  // pulse 7

    // Pulse 8 — should toggle off
    out = g.Process(true);
    CHECK(out == false);
}

TEST_CASE("GateOutput: Init resets state") {
    GateOutput g;
    g.Init();
    g.SetMode(GateOutput::CLOCK_DIV);
    g.SetDivisor(2);

    // Accumulate some state
    g.Process(true); g.Process(false);  // 1 rising edge
    g.Process(true); g.Process(false);  // 2 rising edges — toggled on

    bool out = g.Process(false);
    CHECK(out == true);  // should be high

    // Re-init resets everything — defaults back to passthrough
    g.Init();
    CHECK(g.Process(false) == false);
    CHECK(g.Process(true) == true);   // passthrough mirrors input
}

TEST_CASE("GateOutput: Init defaults to passthrough") {
    GateOutput g;
    g.Init();

    // Should behave as passthrough by default
    CHECK(g.Process(true) == true);
    CHECK(g.Process(false) == false);
}

TEST_CASE("GateOutput: mode change resets counter") {
    GateOutput g;
    g.Init();
    g.SetMode(GateOutput::CLOCK_DIV);
    g.SetDivisor(2);

    // 1 rising edge
    g.Process(true); g.Process(false);

    // Switch mode — should reset internal state
    g.SetMode(GateOutput::PASSTHROUGH);
    g.SetMode(GateOutput::CLOCK_DIV);

    // Need 2 fresh rising edges for first toggle
    g.Process(true); g.Process(false);  // edge 1
    bool out = g.Process(true);          // edge 2 — toggle
    CHECK(out == true);
}

TEST_CASE("GateOutput: clock div ignores held gate (no re-trigger)") {
    GateOutput g;
    g.Init();
    g.SetMode(GateOutput::CLOCK_DIV);
    g.SetDivisor(2);

    // Hold gate high for many samples — should count as 1 rising edge
    g.Process(true);
    g.Process(true);
    g.Process(true);
    g.Process(true);
    bool out = g.Process(true);
    CHECK(out == false);  // still only 1 edge, need 2 to toggle

    // Release and re-trigger — now 2nd edge
    g.Process(false);
    out = g.Process(true);
    CHECK(out == true);  // toggled
}
