# Development Guide

Use C++23 and CMake for the core and CLI. Build out of tree; the workflow must work
without an IDE. Keep common commands in the root Makefile. See the [README](../README.md#build-and-test)
for prerequisites, dependency provisioning, and build/test commands.

## Implementation style

- Prefer standard-library facilities and descriptive names such as `send_file`,
  `transfer_id`, and `peer_endpoint`. Keep functions focused and classes small.
- Use `std::span`, `std::string_view`, `std::filesystem`, `std::chrono`, and value types
  where appropriate. Use `std::optional` or typed errors when they clarify an API.
- Apply the repository's `clang-format` configuration. Avoid macros except where
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
Commit completed work with a descriptive message and push when a remote is configured;
leave unrelated changes unstaged.
