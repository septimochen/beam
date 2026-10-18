# Development Guide

Use C++23 and CMake for the core and CLI. Build out of tree; the workflow must work
without an IDE. Keep common commands in the root Makefile. See the [README](../README.md#quick-start)
for a first transfer.

## Build setup

Requirements: CMake 3.25+, a C++23 compiler, Git, Perl, Make, and OpenSSL development
headers/libraries (1.1.1+). Contributor checks also use clang-format 23 and clang-tidy 23.
Optional Tailscale CLI discovery uses header-only nlohmann_json 3.11+ for bounded
JSON parsing; it adds no runtime service or shared library.
The pinned MsQuic build uses its own quictls TLS implementation; Beam uses the
installed OpenSSL libcrypto for hashing and random transfer IDs.

On macOS, install the build tools with Homebrew:

```sh
brew install cmake openssl@3 llvm nlohmann-json
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

From the repository root:

```sh
make deps   # explicit network download/build of MsQuic v2.6.2
make check  # formatting, clang-tidy/static analysis, build, and tests
```

`make deps` checks commit `819ab74f851ee168504cbc392ec32e7bed1d82e9`, initializes only
its pinned TLS submodule, and installs into ignored `build/deps/install`. Normal
CMake configuration never downloads dependencies. The allowlist `.gitignore`
keeps builds, certificates, keys, and local receive directories untracked.

## Build options

Use Make for a separate Release build:

```sh
make build BUILD_DIR=build-release BUILD_TYPE=Release
make test BUILD_DIR=build-release BUILD_TYPE=Release
```

To use an existing OpenSSL-backed MsQuic installation, pass its prefix to CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSQUIC_ROOT=/path/to/prefix
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

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
includes the private MsQuic runtime; macOS also bundles non-system dylibs. Linux
still uses system OpenSSL/Qt/Avahi packages. See [installation](desktop.md#install-for-your-user)
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

`make routing-test` checks Tailscale status fixtures and subprocess cleanup,
CLI output/Ctrl-C, pure route selection, stored-route updates, and safe authenticated
localhost fallback for files/text/URLs. It verifies that identity failures,
receiver rejection, cancellation, callback exceptions, and post-payload failures
cannot cause a replay. It requires no Tailscale daemon or account. See [Tailscale](tailscale.md).

`make content-test` runs bounded UTF-8/HTTP(S) validation, typed protocol parsing,
and paired localhost text/URL transfers. It covers byte preservation, boundaries,
malformed input, integrity failures, collisions, file-only receiver rejection,
cancellation, callback errors, and publication. These tests are also in `make check`
and CI; they require no discovery daemon. See [content](content.md).

`make pairing-test` runs the identity/store checks and authenticated paired
localhost transfers. These cover fingerprint approval, restart/concurrent
initialization, unsafe state files, address changes, exact leaf pinning, and
revocation. They also run as part of `make check` and Linux CI; no discovery daemon
is needed. See [pairing](pairing.md) for the user workflow.

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
Commit completed work with a descriptive message and push when a remote is configured;
leave unrelated changes unstaged.

## Pairing dependencies and checks

`make deps` also runs `make pairing-deps`. The explicit provisioning script pins
Mbed TLS 4.2.0's official archive and SHA-256, includes its matching TF-PSA-Crypto,
and enables EC-JPAKE plus the required pthread locking. Libraries are linked
statically, separately from MsQuic/quictls. Normal configuration never downloads.
`BEAM_PAIRING_ROOT` selects that provisioned prefix; `BEAM_ENABLE_PAIRING=OFF`
removes this dependency and the network pairing implementation. Manual pairing
and normal transfers remain available. The MIT Nayuki QR encoder v1.8.0 is vendored
under `third_party/qrcodegen` with its license retained. Android additionally pins
CameraX, AndroidX Activity, and the offline Apache-licensed ZXing core.

`make android-deps` provisions ARM64 Mbed TLS too; Android builds support only ARM64.
`make android-setup` installs the latest stable tools, and `make android-toolchain`
shows the shared Make/Gradle selection. See [tool overrides](android.md#build).
`make pairing-test` includes the seamless-pairing test: invitation bounds, PIN
failure/success, reciprocal approval, certificate substitution/key proof, expiry,
replay, attempt limits, IPv4/IPv6, journal idempotency, and real QUIC transfers.
Android JVM tests exercise QR decoding; emulator JNI tests decode a QR produced
by the C++ encoder and persist both full certificate pins across restart.
Physical cameras and LAN interoperability still need device validation.
