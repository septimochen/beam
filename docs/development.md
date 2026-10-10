# Development Guide

Use C++23 and CMake for the core and CLI. Build out of tree; the workflow must work
without an IDE. Keep common commands in the root Makefile. See the [README](../README.md#quick-start)
for a first transfer.

## Build setup

Requirements: CMake 3.25+, a C++23 compiler, Git, Perl, Make, and OpenSSL development
headers/libraries (1.1.1+). Contributor checks also use clang-format 23 and clang-tidy.
The pinned MsQuic build uses its own quictls TLS implementation; Beam uses the
installed OpenSSL libcrypto for hashing and random transfer IDs.

On macOS, install the build tools with Homebrew:

```sh
brew install cmake openssl@3 llvm
export CLANG_FORMAT="$(brew --prefix llvm)/bin/clang-format"
export CLANG_TIDY="$(brew --prefix llvm)/bin/clang-tidy"
```

Formatting requires LLVM 23; check the installed version before running contributor
checks. Bonjour is included with macOS.

On Debian/Ubuntu, install `build-essential`, `libssl-dev`, `perl`, `cmake`, `git`,
`libavahi-compat-libdnssd-dev`, and `avahi-daemon`. Linux discovery requires a
running Avahi daemon. Install `clang-format-23` and `clang-tidy-23` from the
[official LLVM package repository](https://apt.llvm.org/) for your distribution:

```sh
export CLANG_FORMAT=clang-format-23
export CLANG_TIDY=clang-tidy-23
```

From the repository root:

```sh
make deps   # explicit network download/build of MsQuic v2.6.2
make check  # formatting, build, unit tests, and localhost QUIC integration
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
Windows builds only the help/version and filename-validation scaffold by default;
transfer filesystem handling is currently POSIX-only. To select the scaffold on
any platform, disable both features explicitly:

```sh
make build CMAKE_ARGS='-DBEAM_ENABLE_QUIC=OFF -DBEAM_ENABLE_DISCOVERY=OFF'
```

Installing with `cmake --install build --prefix ./install` requires the
MsQuic/OpenSSL shared libraries to remain available to the runtime loader.
Beam does not yet bundle an installer.

## Implementation style

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
to `main`. `make lint` uses Clang Static Analyzer and focused clang-tidy checks for
assertion side effects, duplicated branches, infinite loops, incorrect `sizeof`,
and suspicious expressions/calls, with warnings treated as errors. Install clang-tidy
before running it locally. Opt-in analyzer checks are excluded because the enum-range
check misdiagnoses MsQuic bitmask flags. Override `CLANG_TIDY_CHECKS` to explore other checks.
The full three-platform build/test matrix can be triggered manually in GitHub Actions.
Commit completed work with a descriptive message and push when a remote is configured;
leave unrelated changes unstaged.
