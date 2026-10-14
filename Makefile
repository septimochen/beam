BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE ?= cmake
CTEST ?= ctest
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
CLANG_TIDY_CHECKS ?=
CXX_SOURCES := $(shell find include src tests -name "*.cpp" -o -name "*.hpp" -o -name "*.mm" -o -name "*.h")
SWIFT_SOURCES := $(shell find src tests -name "*.swift")
SWIFT_FORMAT ?= xcrun swift-format
ifeq ($(shell uname -s),Darwin)
DESKTOP_BUILD_DIR ?= $(BUILD_DIR)-desktop-native
DESKTOP_GENERATOR ?= -G Ninja
else
DESKTOP_BUILD_DIR ?= $(BUILD_DIR)-desktop
DESKTOP_GENERATOR ?=
endif
CMAKE_ARGS ?=

.PHONY: all deps configure build test check desktop desktop-test desktop-live-test routing-test content-test pairing-test discovery-test lint format format-check format-version clean help
all: build

deps:
	$(CMAKE) -DBEAM_DEPS_DIR="$(abspath build/deps)" -P cmake/BootstrapMsQuic.cmake

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --parallel --config $(BUILD_TYPE)

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure

check: format-check lint test

desktop:
	$(MAKE) build BUILD_DIR="$(DESKTOP_BUILD_DIR)" CMAKE_ARGS='$(DESKTOP_GENERATOR) $(CMAKE_ARGS) -DBEAM_ENABLE_DESKTOP=ON'

desktop-test:
	$(MAKE) desktop
	$(CTEST) --test-dir "$(DESKTOP_BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure -R '^desktop'

desktop-live-test: desktop
	$(CMAKE) --build "$(DESKTOP_BUILD_DIR)" --target desktop-live-test --config $(BUILD_TYPE)

pairing-test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure -R '^(identity|pairing-localhost)$$'

content-test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure -R '^content(-localhost)?$$'

routing-test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure -R '^(tailscale(-cli)?|routing(-localhost)?)$$'

# Explicit LAN test: requires a running Bonjour/Avahi daemon and multicast access.
discovery-test: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target discovery-test --config $(BUILD_TYPE)

lint: configure
	$(CMAKE) -DBEAM_SOURCE_DIR="$(CURDIR)" -DBEAM_BUILD_DIR="$(abspath $(BUILD_DIR))" \
		-DBEAM_CLANG_TIDY="$(CLANG_TIDY)" -DBEAM_CLANG_TIDY_CHECKS="$(CLANG_TIDY_CHECKS)" \
		-P cmake/RunClangTidy.cmake

format-version:
	@formatter_version="$$($(CLANG_FORMAT) --version)"; \
	case "$$formatter_version" in \
		*"version 23."*) ;; \
		*) echo "Beam requires clang-format 23; set CLANG_FORMAT to its executable (found: $$formatter_version)" >&2; exit 1 ;; \
	esac

format: format-version
	$(CLANG_FORMAT) -i $(CXX_SOURCES)
	@if [ "$$(uname -s)" = Darwin ]; then $(SWIFT_FORMAT) format --in-place $(SWIFT_SOURCES); fi

format-check: format-version
	$(CLANG_FORMAT) --dry-run --Werror $(CXX_SOURCES)
	@if [ "$$(uname -s)" = Darwin ]; then $(SWIFT_FORMAT) lint --strict $(SWIFT_SOURCES); fi

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean --config $(BUILD_TYPE)

help:
	@echo "deps: provision pinned MsQuic; build: configure and compile; test: build and run tests; check: format, lint, and tests"
	@echo "format: apply clang-format; format-check: verify formatting; clean: remove compiled targets"
	@echo "lint: run clang-tidy 23 and static analysis for configured sources using .clang-tidy"
	@echo "discovery-test: live mDNS registration/browse/withdrawal (requires Bonjour/Avahi and LAN access)"
	@echo "pairing-test: identity storage and paired localhost transfers (requires QUIC build)"
	@echo "content-test: typed text/URL validation and paired localhost transfers (requires QUIC build)"
	@echo "routing-test: Tailscale discovery fixtures, route selection and safe localhost fallback"
	@echo "desktop: native Swift macOS (BUILD_DIR-desktop-native) or Qt Linux (BUILD_DIR-desktop); desktop-test: worker and UI checks"
	@echo "desktop-live-test: authenticated desktop worker transfers with live LAN advertisement and withdrawal"
