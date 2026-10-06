BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE ?= cmake
CTEST ?= ctest
CLANG_FORMAT ?= clang-format
CXX_SOURCES := $(shell find include src tests -name "*.cpp" -o -name "*.hpp")
CMAKE_ARGS ?=

.PHONY: all deps configure build test check format format-check clean help
all: build

deps:
	$(CMAKE) -DBEAM_DEPS_DIR="$(abspath build/deps)" -P cmake/BootstrapMsQuic.cmake

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON $(CMAKE_ARGS)

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
	@echo "deps: provision pinned MsQuic; build: configure and compile; test: build and run tests; check: format and tests"
	@echo "format: apply clang-format; format-check: verify formatting; clean: remove compiled targets"
