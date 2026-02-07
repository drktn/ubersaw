CXX      ?= g++
CXXFLAGS  = -std=c++14 -Wall -Wextra -O2 -I../src -I.
LDFLAGS   = -lm

SRC_DIR   = ../src

TEST_BINS = test_runner test_smooth_runner test_gate_runner test_saturator_runner test_sub_osc_runner test_lfo_runner test_envelope_runner

SRCS_SUPERSAW = test_supersaw.cpp $(SRC_DIR)/supersaw.cpp
SRCS_SMOOTH   = test_smooth.cpp
SRCS_GATE     = test_gate.cpp
SRCS_SAT      = test_saturator.cpp
SRCS_SUB_OSC  = test_sub_osc.cpp
SRCS_LFO      = test_lfo.cpp
SRCS_ENV      = test_envelope.cpp

.PHONY: all clean test

all: $(TEST_BINS)

test_runner: $(SRCS_SUPERSAW)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_smooth_runner: $(SRCS_SMOOTH)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_gate_runner: $(SRCS_GATE)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_saturator_runner: $(SRCS_SAT)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_sub_osc_runner: $(SRCS_SUB_OSC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_lfo_runner: $(SRCS_LFO)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_envelope_runner: $(SRCS_ENV)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TEST_BINS)
	./test_runner
	./test_smooth_runner
	./test_gate_runner
	./test_saturator_runner
	./test_sub_osc_runner
	./test_lfo_runner
	./test_envelope_runner

clean:
	rm -f $(TEST_BINS)
