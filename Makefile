BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE ?= cmake
CTEST ?= ctest
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
CLANG_TIDY_CHECKS ?=
CXX_SOURCES := $(shell find include src tests cmake -name "*.cpp" -o -name "*.hpp" -o -name "*.mm" -o -name "*.h")
SWIFT_SOURCES := $(shell find src tests tools -name "*.swift")
JAVA_SOURCES := $(shell find android/app/src -name "*.java")
SWIFT_FORMAT ?= xcrun swift-format
ifeq ($(shell uname -s),Darwin)
DESKTOP_BUILD_DIR ?= $(BUILD_DIR)/macos/$(if $(filter Release,$(BUILD_TYPE)),release,dev)
DESKTOP_GENERATOR ?= -G Ninja
else
DESKTOP_BUILD_DIR ?= $(BUILD_DIR)-desktop
DESKTOP_GENERATOR ?=
endif
CMAKE_ARGS ?=
ANDROID_ABI ?= arm64-v8a
ANDROID_DEPS_DIR ?= $(CURDIR)/build/android-deps
ANDROID_GRADLE_ARGS ?=
# Keep Make, direct Gradle builds, and Android Studio on the same tool selection.
export ANDROID_ABI ANDROID_HOME ANDROID_NDK ANDROID_PLATFORM_VERSION
export ANDROID_BUILD_TOOLS_VERSION ANDROID_NDK_VERSION ANDROID_CMAKE_VERSION
export ANDROID_SETUP_DRY_RUN SDKMANAGER
export BEAM_ANDROID_SIGNING_PROPERTIES
ifeq ($(shell uname -s),Darwin)
INSTALL_BUILD_DIR ?= build/macos/release
else
INSTALL_BUILD_DIR ?= build-install
endif
MACOS_BUNDLE_ID ?= org.beam.desktop
MACOS_CLEANUP_ARGS ?= --dry-run
INSTALL_PREFIX ?= $(HOME)/.local
APPLICATIONS_DIR ?= $(HOME)/Applications
DESKTOP_ENTRY_DIR ?= $(if $(XDG_DATA_HOME),$(XDG_DATA_HOME),$(HOME)/.local/share)/applications
ADB ?= $(if $(ANDROID_HOME),$(ANDROID_HOME)/platform-tools/adb,adb)
ANDROID_SERIAL ?=
ANDROID_APK ?= $(CURDIR)/android/app/build/outputs/apk/debug/app-debug.apk

.PHONY: all deps deps-source pairing-deps configure build test check icons icons-native install uninstall macos-cleanup android-install android-install-apk android android-release android-release-unsigned android-setup android-toolchain android-deps android-native-test android-check android-format android-format-check android-connected-test desktop desktop-test desktop-live-test routing-test content-test pairing-test devices-test discovery-test lint format format-check format-version clean help
all: build

deps:
	$(CMAKE) --fresh -S cmake/msquic-check -B "$(BUILD_DIR)/msquic-check" $(CMAKE_ARGS)
	$(MAKE) pairing-deps

deps-source:
	$(CMAKE) -DBEAM_DEPS_DIR="$(abspath build/deps)" -P cmake/BootstrapMsQuic.cmake
	$(MAKE) pairing-deps

pairing-deps:
	$(CMAKE) -DBEAM_DEPS_DIR="$(abspath build/deps)" -P cmake/BootstrapPairing.cmake

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --parallel --config $(BUILD_TYPE)

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure

check: format-check lint test

icons:
	xcrun swift tools/GenerateIcons.swift "$(CURDIR)"

icons-native:
	$(CMAKE) -DBEAM_SOURCE_DIR="$(CURDIR)" -DBEAM_ICON_OUTPUT="$(abspath build/icons/native)" -P cmake/CompileMacIcon.cmake

install:
	$(CMAKE) -S . -B "$(INSTALL_BUILD_DIR)" $(DESKTOP_GENERATOR) $(CMAKE_ARGS) \
		-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBEAM_ENABLE_DESKTOP=ON \
		-DCMAKE_INSTALL_PREFIX="$(INSTALL_PREFIX)" -DCMAKE_INSTALL_LIBDIR=lib \
		-DBEAM_MACOS_BUNDLE_ID="$(MACOS_BUNDLE_ID)" \
		-DBEAM_DESKTOP_ENTRY_DIR="$(DESKTOP_ENTRY_DIR)"
	$(CMAKE) --build "$(INSTALL_BUILD_DIR)" --parallel --config Release
	$(CMAKE) --install "$(INSTALL_BUILD_DIR)" --config Release --prefix "$(INSTALL_PREFIX)" --component cli
ifeq ($(shell uname -s),Darwin)
	$(CMAKE) --install "$(INSTALL_BUILD_DIR)" --config Release --prefix "$(APPLICATIONS_DIR)" --component desktop
else
	$(CMAKE) --install "$(INSTALL_BUILD_DIR)" --config Release --prefix "$(INSTALL_PREFIX)" --component desktop
endif
	@echo 'Installed Beam. Add $(INSTALL_PREFIX)/bin to PATH if needed.'

uninstall:
	$(CMAKE) -DINSTALL_PREFIX="$(INSTALL_PREFIX)" -DAPPLICATIONS_DIR="$(APPLICATIONS_DIR)" \
		-DBEAM_MACOS_BUNDLE_ID="$(MACOS_BUNDLE_ID)" \
		-DDESKTOP_ENTRY_DIR="$(DESKTOP_ENTRY_DIR)" -P cmake/Uninstall.cmake

macos-cleanup:
	./scripts/cleanup-macos.sh $(MACOS_CLEANUP_ARGS)

android-install: android
	$(MAKE) android-install-apk

android-install-apk:
	$(CMAKE) -DADB="$(ADB)" -DANDROID_SERIAL="$(ANDROID_SERIAL)" \
		-DANDROID_APK="$(ANDROID_APK)" -P cmake/InstallAndroid.cmake

android-setup:
	$(CMAKE) -DBEAM_ANDROID_ACTION=setup -P cmake/AndroidTools.cmake

android-toolchain:
	$(CMAKE) -P cmake/AndroidTools.cmake

android-deps:
	$(CMAKE) -DBEAM_ANDROID_ACTION=deps -DBEAM_ANDROID_DEPS="$(ANDROID_DEPS_DIR)" -P cmake/AndroidTools.cmake

android:
	./android/gradlew -p android assembleDebug $(ANDROID_GRADLE_ARGS)

android-release:
	./android/gradlew -p android assembleRelease $(ANDROID_GRADLE_ARGS) -PbeamUnsignedRelease=false
	bash tests/android_release_apk.sh "$(CURDIR)/android/app/build/outputs/apk/release/app-release.apk" signed

android-release-unsigned:
	./android/gradlew -p android assembleRelease $(ANDROID_GRADLE_ARGS) -PbeamUnsignedRelease=true
	bash tests/android_release_apk.sh "$(CURDIR)/android/app/build/outputs/apk/release/app-release-unsigned.apk" unsigned

android-native-test:
	$(CMAKE) -DBEAM_ANDROID_ACTION=test -DBEAM_ANDROID_DEPS="$(ANDROID_DEPS_DIR)" -P cmake/AndroidTools.cmake

android-check:
	bash tests/android_release_signing.sh $(ANDROID_GRADLE_ARGS)
	./android/gradlew -p android formatCheck lintDebug testDebugUnitTest assembleDebug $(ANDROID_GRADLE_ARGS)

android-format:
	./android/gradlew -p android formatKotlin

android-format-check:
	./android/gradlew -p android formatCheck

android-connected-test:
	./android/gradlew -p android connectedDebugAndroidTest $(ANDROID_GRADLE_ARGS)

desktop:
	$(MAKE) build BUILD_DIR="$(DESKTOP_BUILD_DIR)" CMAKE_ARGS='$(DESKTOP_GENERATOR) $(CMAKE_ARGS) -DBEAM_ENABLE_DESKTOP=ON'

desktop-test:
	$(MAKE) desktop
	$(CTEST) --test-dir "$(DESKTOP_BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure -R '^desktop'

desktop-live-test: desktop
	$(CMAKE) --build "$(DESKTOP_BUILD_DIR)" --target desktop-live-test --config $(BUILD_TYPE)

pairing-test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure -R '^(identity|pairing-localhost|seamless-pairing)$$'

devices-test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --build-config $(BUILD_TYPE) --output-on-failure -R '^device-management(-cli)?$$'

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
	$(CLANG_FORMAT) -i $(CXX_SOURCES) $(JAVA_SOURCES)
	@if [ "$$(uname -s)" = Darwin ]; then $(SWIFT_FORMAT) format --in-place $(SWIFT_SOURCES); fi

format-check: format-version
	$(CLANG_FORMAT) --dry-run --Werror $(CXX_SOURCES) $(JAVA_SOURCES)
	@if [ "$$(uname -s)" = Darwin ]; then $(SWIFT_FORMAT) lint --strict $(SWIFT_SOURCES); fi

clean:
	$(CMAKE) -DBUILD_DIR="$(BUILD_DIR)" -DDESKTOP_BUILD_DIR="$(DESKTOP_BUILD_DIR)" \
		-DINSTALL_BUILD_DIR="$(INSTALL_BUILD_DIR)" -P cmake/Clean.cmake

help:
	@echo "icons: regenerate shared icon exports on macOS; icons-native: compile native appearances with Xcode actool"
	@echo "install: Release CLI to ~/.local/bin, plus Linux launcher or macOS ~/Applications/Beam.app (no sudo)"
	@echo "uninstall: remove installed Beam binaries, runtime and app/launcher; keep settings, pairing and received files"
	@echo "macos-cleanup: inspect app registrations; MACOS_CLEANUP_ARGS=--remove-build-apps removes obsolete generated app bundles"
	@echo "android-install: build and install debug APK with ADB; android-install-apk: install existing APK; ANDROID_SERIAL selects a device"
	@echo "deps: check system MsQuic and provision pairing; deps-source: explicitly build pinned MsQuic and pairing"
	@echo "build: configure and compile; test: build and run tests; check: format, lint, and tests"
	@echo "format: apply clang-format; format-check: verify formatting"
	@echo "clean: delete build/, build-* and Android build output, including local dependencies/toolchains"
	@echo "lint: run clang-tidy 23 and static analysis for configured sources using .clang-tidy"
	@echo "discovery-test: live mDNS registration/browse/withdrawal (requires Bonjour/Avahi and LAN access)"
	@echo "pairing-test: identity storage and paired localhost transfers (requires QUIC build)"
	@echo "devices-test: device edits, certificate replacement, backup restore and CLI checks"
	@echo "content-test: text and connectivity validation and paired localhost transfers (requires QUIC build)"
	@echo "routing-test: Tailscale discovery fixtures, route selection and safe localhost fallback"
	@echo "desktop: native Swift macOS (BUILD_DIR/macos/dev) or Qt Linux (BUILD_DIR-desktop); desktop-test: worker and UI checks"
	@echo "macos-cleanup: inspect Beam build registrations; MACOS_CLEANUP_ARGS=--development requests targeted unregistration"
	@echo "desktop-live-test: authenticated desktop worker transfers with live LAN advertisement and withdrawal"
	@echo "android-setup: install latest stable SDK/NDK/CMake tools; android-toolchain: show selected installed versions"
	@echo "android-deps: pinned native dependencies for ARM64; android: ARM64 debug APK"
	@echo "android-release: signed optimized ARM64 APK; android-release-unsigned: unsigned APK for validation/CI"
	@echo "android-native-test: configure Android ARM64 with incompatible host OpenSSL metadata"
	@echo "android-check: signing validation, Kotlin format, Android lint, unit tests and APK; android-connected-test: device/emulator JNI and UI tests"
