BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE ?= cmake
CTEST ?= ctest
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
CLANG_TIDY_CHECKS ?= -*,clang-analyzer-*,-clang-analyzer-optin.*,bugprone-assert-side-effect,bugprone-branch-clone,bugprone-infinite-loop,bugprone-sizeof-expression,bugprone-suspicious-*
CXX_SOURCES := $(shell find include src tests -name "*.cpp" -o -name "*.hpp")
CXX_TRANSLATION_UNITS := $(filter %.cpp,$(CXX_SOURCES))
CMAKE_ARGS ?=

.PHONY: all deps configure build test check discovery-test lint format format-check format-version clean help
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

# Explicit LAN test: requires a running Bonjour/Avahi daemon and multicast access.
discovery-test: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target discovery-test --config $(BUILD_TYPE)

lint: configure
	@set -e; for source in $(CXX_TRANSLATION_UNITS); do \
		$(CLANG_TIDY) -p "$(BUILD_DIR)" --checks='$(CLANG_TIDY_CHECKS)' \
			--header-filter='^$(CURDIR)/(include|src|tests)/' --warnings-as-errors='*' "$$source"; \
	done

format-version:
	@formatter_version="$$($(CLANG_FORMAT) --version)"; \
	case "$$formatter_version" in \
		*"version 23."*) ;; \
		*) echo "Beam requires clang-format 23; set CLANG_FORMAT to its executable (found: $$formatter_version)" >&2; exit 1 ;; \
	esac

format: format-version
	$(CLANG_FORMAT) -i $(CXX_SOURCES)

format-check: format-version
	$(CLANG_FORMAT) --dry-run --Werror $(CXX_SOURCES)

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean --config $(BUILD_TYPE)

help:
	@echo "deps: provision pinned MsQuic; build: configure and compile; test: build and run tests; check: format and tests"
	@echo "format: apply clang-format; format-check: verify formatting; clean: remove compiled targets"
	@echo "lint: configure and run clang-tidy lint and static analysis (requires clang-tidy)"
	@echo "discovery-test: live mDNS registration/browse/withdrawal (requires Bonjour/Avahi and LAN access)"
