CXX      ?= g++
CXXFLAGS  = -std=c++14 -Wall -Wextra -O2 -I../src -I.
LDFLAGS   = -lm

SRC_DIR   = ../src

TEST_BINS = test_runner test_smooth_runner test_gate_runner

SRCS_SUPERSAW = test_supersaw.cpp $(SRC_DIR)/supersaw.cpp
SRCS_SMOOTH   = test_smooth.cpp
SRCS_GATE     = test_gate.cpp

.PHONY: all clean test

all: $(TEST_BINS)

test_runner: $(SRCS_SUPERSAW)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_smooth_runner: $(SRCS_SMOOTH)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_gate_runner: $(SRCS_GATE)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TEST_BINS)
	./test_runner
	./test_smooth_runner
	./test_gate_runner

clean:
	rm -f $(TEST_BINS) test_gate_runner
