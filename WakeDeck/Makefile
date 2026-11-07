# WakeDeck - Milestone 1: command-line wake prototype (no Qt yet).
# Same compiler flags as the other projects in this repository.
#
#   make                 build build/wakedeck-cli
#   make test            build and run the unit tests and the loopback test
#   make run ARGS="..."  run the tool (default: --help)
#   make clean           remove the build directory
#
# BUILD_DIR can be overridden, e.g.  make BUILD_DIR=/tmp/wakedeck-build

CXX       ?= c++
CXXFLAGS  ?= -std=c++17 -Wall -Wextra -pedantic -O2
CPPFLAGS  += -Isrc -MMD -MP
BUILD_DIR ?= build

CORE_SRCS := $(wildcard src/core/*.cpp)
CORE_OBJS := $(CORE_SRCS:src/%.cpp=$(BUILD_DIR)/%.o)
CLI_SRCS  := $(wildcard src/cli/*.cpp)
CLI_OBJS  := $(CLI_SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

CLI   := $(BUILD_DIR)/wakedeck-cli
TESTS := $(BUILD_DIR)/test_core $(BUILD_DIR)/test_loopback
ARGS  ?= --help

.PHONY: all cli test run clean

all: cli

cli: $(CLI)

$(CLI): $(CORE_OBJS) $(CLI_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_%: tests/test_%.cpp tests/check.h $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(CORE_OBJS) -o $@

test: $(TESTS) $(CLI)
	@for t in $(TESTS); do echo "== $$t"; "$$t" || exit 1; done
	@echo "== tests/cli_test.sh"; sh tests/cli_test.sh $(CLI)

run: cli
	$(CLI) $(ARGS)

clean:
	rm -rf $(BUILD_DIR)

-include $(CORE_OBJS:.o=.d) $(CLI_OBJS:.o=.d)
