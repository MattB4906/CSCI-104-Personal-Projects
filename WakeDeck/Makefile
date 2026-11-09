# WakeDeck - the Makefile is the entry point, as in the other projects of this
# repository; underneath it drives CMake, which Qt needs.
#
#   make                 configure (first time) and build everything into build/
#   make test            build, then run every test through ctest
#   make run ARGS="..."  run build/wakedeck-cli (default: --help)
#   make run-app         build, then open build/WakeDeck.app
#   make release QT_DIR=/path/to/Qt/6.11.3/macos
#                        signed, notarized disk image (see packaging/make-release.sh)
#   make clean           remove the build directory
#
# Requirements: CMake 3.21+, Qt 6 (Core, Network, Test), Ninja (optional).
# With Homebrew:  brew install cmake ninja qt
# Overridable: BUILD_DIR, BUILD_TYPE, JOBS, CMAKE, CTEST, GENERATOR, QT_PREFIX, ARGS.

BUILD_DIR  ?= build
BUILD_TYPE ?= Release
JOBS       ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 2)
ARGS       ?= --help

# Find the tools even when Homebrew is not on PATH yet.
BREW      := $(shell command -v brew 2>/dev/null || { [ -x /opt/homebrew/bin/brew ] && echo /opt/homebrew/bin/brew; })
CMAKE     ?= $(shell command -v cmake 2>/dev/null || echo /opt/homebrew/bin/cmake)
CTEST     ?= $(shell command -v ctest 2>/dev/null || echo $(dir $(CMAKE))ctest)
NINJA     := $(shell command -v ninja 2>/dev/null || { [ -x /opt/homebrew/bin/ninja ] && echo /opt/homebrew/bin/ninja; })
QT_PREFIX ?= $(if $(BREW),$(shell $(BREW) --prefix qt 2>/dev/null),)
GENERATOR ?= $(if $(NINJA),Ninja,Unix Makefiles)

CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
ifneq ($(QT_PREFIX),)
CMAKE_FLAGS += -DCMAKE_PREFIX_PATH=$(QT_PREFIX)
endif
ifneq ($(NINJA),)
CMAKE_FLAGS += -DCMAKE_MAKE_PROGRAM=$(NINJA)
endif

.PHONY: all configure build test run run-app release clean

all: build

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	$(CMAKE) -S . -B $(BUILD_DIR) -G "$(GENERATOR)" $(CMAKE_FLAGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)

test: build
	cd $(BUILD_DIR) && $(CTEST) --output-on-failure

run: build
	$(BUILD_DIR)/wakedeck-cli $(ARGS)

run-app: build
	open $(BUILD_DIR)/WakeDeck.app

release:
	QT_DIR="$(QT_DIR)" sh packaging/make-release.sh

clean:
	rm -rf $(BUILD_DIR)
