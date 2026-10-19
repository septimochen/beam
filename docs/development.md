# Development Guide

Use C++23 and CMake for the core and CLI. Build out of tree; the workflow must work
without an IDE. Keep common commands in the root Makefile. See the [README](../README.md#quick-start)
for a first transfer.

## Build setup

Requirements: CMake 3.25+, a C++23 compiler, Git, Perl, Make, and OpenSSL development
headers/libraries (1.1.1+). Contributor checks also use clang-format 23 and clang-tidy 23.
Optional Tailscale CLI discovery uses header-only nlohmann_json 3.11+ for bounded
JSON parsing; it adds no runtime service or shared library.
Install an OpenSSL-backed MsQuic 2.x runtime, version 2.6.2 or newer, through your
system package manager. Beam also uses installed OpenSSL libcrypto for hashing,
certificate verification, and random transfer IDs.

On macOS, install the build tools with Homebrew:

```sh
brew install cmake libmsquic openssl@3 llvm nlohmann-json
export CLANG_FORMAT="$(brew --prefix llvm)/bin/clang-format"
export CLANG_TIDY="$(brew --prefix llvm)/bin/clang-tidy"
```

Formatting and linting require LLVM 23; check the installed versions before running
contributor checks. Bonjour is included with macOS.

On Debian/Ubuntu, install `build-essential`, `libssl-dev`, `perl`, `cmake`, `git`,
`libavahi-compat-libdnssd-dev`, `avahi-daemon`, and `nlohmann-json3-dev`. Linux discovery requires a
running Avahi daemon. Install `clang-format-23` and `clang-tidy-23` from the
[official LLVM package repository](https://apt.llvm.org/) for your distribution:

```sh
export CLANG_FORMAT=clang-format-23
export CLANG_TIDY=clang-tidy-23
```

Install MsQuic from your distribution if a compatible package is available.
Otherwise configure [Microsoft's Linux package repository](https://learn.microsoft.com/en-us/linux/packages)
for your distribution/version using its official repository package and signing
key, then run:

```sh
# Debian/Ubuntu, after configuring Microsoft's repository:
sudo apt update
sudo apt install libmsquic
# Fedora/RHEL, after configuring Microsoft's repository:
sudo dnf install libmsquic
```

See [upstream distribution support](https://github.com/microsoft/msquic/blob/main/docs/Release.md)
before choosing a repository. On Arch, install normal build prerequisites with
`pacman`; MsQuic is available through the [msquic AUR package](https://aur.archlinux.org/packages/msquic),
not the default repositories. As checked on 2026-10-19, that package is 2.4.11,
below Beam's minimum. Use an updated compatible package or the explicit source
fallback below. Beam does not install an AUR helper or add package repositories.

From the repository root:

```sh
make deps   # validate system MsQuic, then download/build the pairing dependency
make check  # formatting, clang-tidy/static analysis, build, and tests
```

`make deps` prints setup guidance when MsQuic is missing or incompatible; it never
installs packages, changes repositories, or invokes `sudo`. Homebrew discovery
only queries `brew --prefix libmsquic`. Native CMake configuration and `make deps`
share an offline API/link/runtime-version probe and print the selected library,
headers, and version. Homebrew's prefix is detected automatically; Linux searches
normal system, multiarch, and `lib64` locations, including `libmsquic.so.2` without
an unversioned linker symlink. Installed SDK headers are preferred. Runtime-only
DEB/RPM packages use the three unmodified, MIT-licensed v2.6.2 POSIX public headers
in `third_party/msquic`; Beam does not compile an MsQuic implementation from them.
Android continues to use its pinned per-ABI archives and headers.

Normal CMake configuration never downloads dependencies. The allowlist `.gitignore`
keeps builds, certificates, keys, and local receive directories untracked.

## Build options

`make clean` deletes `build/`, root-level `build-*` directories, and Android's
`android/build`, `android/app/build`, and `android/app/.cxx` output. It also deletes
project-local downloaded dependencies and SDK/JDK/tool caches stored under `build/`.
It succeeds before configuration and on repeated runs. Run `make deps` (and the
Android setup/dependency commands when needed) again before rebuilding. Custom
`BUILD_DIR`, `DESKTOP_BUILD_DIR`, and `INSTALL_BUILD_DIR` paths are also removed
when they contain a CMake cache identifying this Beam checkout. Source folders,
installed applications, system packages, and tools outside these build paths stay
in place. For CMake's narrower object-file cleanup, use
`cmake --build BUILD_DIR --target clean` on a configured tree.

Use Make for a separate Release build:

```sh
make build BUILD_DIR=build-release BUILD_TYPE=Release
make test BUILD_DIR=build-release BUILD_TYPE=Release
```

To use a custom OpenSSL-backed MsQuic installation, pass its prefix to CMake.
Use a **fresh build directory** when switching between system/custom/source
installations: CMake caches `MSQUIC_ROOT`, `MSQUIC_INCLUDE_DIR`, and `MSQUIC_LIBRARY`.
The latter two remain available as explicit overrides.

```sh
cmake -S . -B build-custom -DCMAKE_BUILD_TYPE=Release -DMSQUIC_ROOT=/path/to/prefix
cmake --build build-custom --parallel
ctest --test-dir build-custom --output-on-failure
```

For the explicit source fallback:

```sh
make deps-source
make test BUILD_DIR=build-source \
  CMAKE_ARGS="-DMSQUIC_ROOT=$PWD/build/deps/install -DBEAM_BUNDLE_MSQUIC=ON"
```

This pins MsQuic v2.6.2 at commit `819ab74f851ee168504cbc392ec32e7bed1d82e9`,
initializes only its quictls TLS submodule, and installs into ignored
`build/deps/install`; it also provisions pairing. That prefix is never searched
implicitly. On Linux, `BEAM_BUNDLE_MSQUIC=ON` bundles the custom runtime for
installation; default installations depend on the system loader's MsQuic package.
For `make install`, pass the same `CMAKE_ARGS` and a fresh `INSTALL_BUILD_DIR`.

Discovery defaults to the QUIC setting. To build direct transfers without
Bonjour/Avahi, use `make build CMAKE_ARGS='-DBEAM_ENABLE_DISCOVERY=OFF'`.
Tailscale discovery also defaults to QUIC. Disable its CLI/JSON dependency with
`-DBEAM_ENABLE_TAILSCALE=OFF`; manual numeric Tailscale routes remain available.
Windows builds only the help/version and filename-validation scaffold by default;
transfer filesystem handling is currently POSIX-only. To select the scaffold on
any platform, disable all optional features explicitly:

```sh
make build CMAKE_ARGS='-DBEAM_ENABLE_QUIC=OFF -DBEAM_ENABLE_DISCOVERY=OFF -DBEAM_ENABLE_TAILSCALE=OFF'
```

Use `make install` for a user-local Release CLI and desktop installation. It
uses system MsQuic/OpenSSL/Qt/Avahi packages on Linux; package upgrades update the
MsQuic runtime Beam loads. macOS bundles non-system dylibs, including MsQuic.
See [installation](desktop.md#install-for-your-user)
for destinations, prefix overrides, and CMake install components. `make uninstall`
removes that installation without a build directory, retaining user data. CTest's
`install` check stages and relocates the installation, runs its binaries, then
checks removal; `uninstall` tests custom paths, staging, and preservation of other files.

The optional desktop app requires the QUIC core. macOS requires full Xcode for
Swift 6 and native icon compilation, plus Ninja (`brew install ninja`); Qt is not
required.
The deployment version follows the selected macOS SDK, including cached desktop
builds formerly targeting 13.0. Swift bridge PCHs stay in the build directory,
and the shared C++ runtime is linked once across the C++/Objective-C++ boundary.
Linux uses Qt 6.4+ Widgets (`qt6-base-dev` and `qt6-qpa-plugins` on Debian/Ubuntu).
Use `make desktop` / `make desktop-test`. Native macOS builds use Ninja and a
separate `build/macos/dev` directory, keeping existing Qt build caches separate.
The [macOS lifecycle guide](macos-lifecycle.md) covers development/production bundle
identities, CMake presets, safe upgrades, and targeted registration cleanup.
A normal CLI build has no Swift or Qt dependency. Main-branch Linux CI and manual
macOS/Linux builds enable `BEAM_ENABLE_DESKTOP`; Linux UI tests use Qt's offscreen
platform, while macOS tests exercise the Swift model and Objective-C++ bridge.
`make format` / `make format-check` also use Apple's bundled `xcrun swift-format`
on macOS; `make lint` includes configured Objective-C++ translation units. See
[desktop](desktop.md) for launch paths, clipboard privacy, and native checks.

`make icons` regenerates the committed platform icon exports from their shared
Swift geometry source on macOS. `make icons-native` compiles the layered macOS
catalog with full Xcode's asset compiler. Every macOS desktop build also compiles
that catalog automatically. See [icon sources](../assets/icons/README.md) for
Xcode selection and native appearance verification.

## Implementation style

Android build and validation use `make android-deps`, `make android-check`, and
`make android-connected-test`; see [Android](android.md). The app uses AGP's
built-in Kotlin, Android platform UI/service/document APIs, and the existing
C++ core. ktfmt 0.64 is a build-only formatter; AndroidX Test Runner and JUnit 4 are test-only
dependency. Android libc++ stop-token/jthread support uses `-fexperimental-library`;
this is enabled only for Android. Default CLI builds require no JDK or SDK.

- Prefer standard-library facilities and descriptive names such as `send_file`,
  `transfer_id`, and `peer_endpoint`. Keep functions focused and classes small.
- Use `std::span`, `std::string_view`, `std::filesystem`, `std::chrono`, and value types
  where appropriate. Use `std::optional` or typed errors when they clarify an API.
- Apply the repository's `clang-format` configuration using clang-format 23, matching
  CI. `make format` and `make format-check` reject other major versions; use
  `CLANG_FORMAT` to select the executable. Avoid macros except where
  platform APIs or build configuration require them.
- Modify existing components before adding new systems. Add directories only when
  functionality needs them; do not scaffold hypothetical native apps or services.
- Introduce an interface only for an actual implementation boundary or a clear
  testing benefit. The transport boundary is intentional so QUIC backends can change.
- Explain why a new dependency is necessary. Prefer the simplest design that is safe,
  testable, and replaceable. Optimize only after measuring a problem.

## Ownership and concurrency

Wrap native resources in RAII, including QUIC handles, files, sockets, and threads.
Prefer values and `std::unique_ptr`; use `std::shared_ptr` only for actual shared
ownership. Raw pointers may express non-owning references, but must not own resources.
Callers should not manually release transport resources.

Networking is asynchronous even when the application boundary offers blocking calls.
Keep callback contexts alive until all callbacks finish, and preserve orderly shutdown
before closing native handles. Do not detach threads. If application threads are needed,
prefer `std::jthread`, `std::stop_token`, explicit cancellation, and scoped lifetimes.
See [architecture](architecture.md) for the current MsQuic lifecycle.

## Errors and logging

Do not ignore failures. Translate transport errors at boundaries, retain useful peer
and operation context, and never throw through C callbacks. Prefer an explanation such
as "QUIC handshake timed out" over an unexplained numeric error code.

Diagnostic logging may include peer, connection/transfer/stream IDs, byte counts,
state transitions, and errors. Never log private keys, authentication secrets, full
file contents, or clipboard contents by default. Remote errors should not expose
local filesystem details.

## Testing and completion

Focused checks (included in `make check` when their features are enabled):

| Command | Coverage |
| --- | --- |
| `make pairing-test` | Identity storage, exact certificate pins, LAN pairing and recovery |
| `make devices-test` | Device edits, certificate replacement, backup restore and CLI |
| `make content-test` | Bounded UTF-8 text, integrity and paired localhost transfers |
| `make routing-test` | Route ordering, authenticated fallback and subprocess cleanup |
| `make desktop-test` | Shared workers and platform UI; see [desktop verification](desktop.md#verification) |

Localhost integration checks require socket access, but no public network or
discovery daemon. Live LAN checks are separate: `make discovery-test` and
`make desktop-live-test` require Bonjour/Avahi and multicast access.

Run `make check` before completing code changes. Add focused tests for changes to
protocol parsing, transfer states, input validation, or integrity checks. Cover:

- Encoding/decoding, fragmentation, malformed messages, and bounds checks.
- Filename validation, directory containment, symlinks, and collision protection.
- State transitions, checksums, and endpoint selection as those features evolve.
- Successful transfers and interrupted, rejected, invalid, or timed-out transfers.

Integration tests should transfer a temporary file over localhost QUIC, compare the
received bytes, verify integrity, and clean up. Tests must not depend on public internet
connectivity. Dependency provisioning is a separate, explicit operation.

A feature is complete when it builds on applicable supported platforms, handles failures,
cleans up resources, avoids data loss, produces useful errors, preserves architecture
boundaries, and has appropriate tests. Record platform validation limits honestly.
Documentation-only changes need link/content checks rather than new executable tests.
The automatic CI status check runs formatting, `make lint`, and tests only on pushes
to `main`. `make check` runs the same categories locally. `make lint` uses LLVM 23
and the root `.clang-tidy` configuration, with warnings treated as errors. It runs
Clang Static Analyzer plus focused checks for unused direct includes,
assertion side effects, duplicated branches, dangling views, use after move, unused
RAII temporaries, unguarded condition-variable waits, unchecked optional access, and
suspicious expressions.
Opt-in analyzer checks are excluded because the enum-range check misdiagnoses
MsQuic bitmask flags. Override `CLANG_TIDY_CHECKS` to explore other checks.

Lint selects source files from the configured `compile_commands.json`, so disabled
QUIC, discovery, Tailscale, or desktop components are not analyzed using unrelated
compiler flags. Use a Makefiles or Ninja generator. For the full desktop build:

```sh
make check BUILD_DIR=build-desktop CMAKE_ARGS='-DBEAM_ENABLE_DESKTOP=ON'
```

On macOS, the lint runner locates the SDK through `xcrun` and passes it to
clang-tidy; an explicit SDK in the compilation flags takes precedence. Editor
clang-tidy integrations should use the same configuration and compilation database.
Static analysis complements the focused integration/UI tests: it cannot prove
correct callback lifetimes, deadlock freedom, or platform clipboard/drop policy.
The full three-platform build/test matrix can be triggered manually in GitHub Actions.
CI caches pinned native dependencies and Gradle downloads, and cancels superseded
runs on the same ref/event. Native cache keys include provisioning/configuration
inputs and Android's selected NDK/CMake versions; host caches also track the runner
image. Provisioning remains explicit, and all build/test categories are retained.
Commit completed work with a descriptive message and push when a remote is configured;
leave unrelated changes unstaged.

## Pairing dependencies

`make deps` / `make deps-source` also run `make pairing-deps`, provisioning
checksum-pinned Mbed TLS 4.2.0 and its matching TF-PSA-Crypto with EC-JPAKE and
pthread locking. These static libraries are separate from MsQuic/quictls.
`BEAM_PAIRING_ROOT` selects the installed prefix. `BEAM_ENABLE_PAIRING=OFF`
removes network pairing and this dependency; manual PEM pairing and transfers
remain available. Normal configuration never downloads dependencies.

The MIT Nayuki QR encoder v1.8.0 is vendored with its license. Android's pinned
CameraX, AndroidX Activity and ZXing dependencies, tool selection, APK packaging,
and checks are documented in [Android](android.md). Pairing authentication,
recovery and validation are documented in [pairing](pairing.md).
