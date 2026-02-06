CXX      ?= g++
CXXFLAGS  = -std=c++14 -Wall -Wextra -O2 -I../src -I.
LDFLAGS   = -lm

SRC_DIR   = ../src
TEST_BIN  = test_runner

SRCS      = test_supersaw.cpp $(SRC_DIR)/supersaw.cpp
OBJS      = $(SRCS:.cpp=.o)

.PHONY: all clean test

all: $(TEST_BIN)

$(TEST_BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_BIN)
