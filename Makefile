BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE ?= cmake
CTEST ?= ctest
CLANG_FORMAT ?= clang-format
CXX_SOURCES := $(wildcard include/beam/*.hpp src/core/*.cpp src/cli/*.cpp tests/*.cpp)

.PHONY: all configure build test check format format-check clean help
all: build

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --parallel --config $(BUILD_TYPE)

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure

check: format-check test

format:
	$(CLANG_FORMAT) -i $(CXX_SOURCES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(CXX_SOURCES)

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean --config $(BUILD_TYPE)

help:
	@echo "build: configure and compile; test: build and run tests; check: format and tests"
	@echo "format: apply clang-format; format-check: verify formatting; clean: remove compiled targets"
