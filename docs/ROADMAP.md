# Roadmap

## Bootstrap

- [x] C++23/CMake core and CLI targets, out-of-tree builds, installation.
- [x] Make build, test, check, and formatting commands; allowlist gitignore.
- [x] Portable filename rejection with malicious-input and boundary tests.
- [x] CLI help/version and explicit errors for unavailable transfers.
- [x] CTest and macOS/Linux/Windows CI configuration.

## First reliable direct transfer

- [x] Integrate MsQuic behind an internal transport boundary with RAII and safe callbacks.
- [x] Define certificate provisioning and verified peers for the CLI milestone.
- [x] Define bounded versioned metadata/control framing; test malformed and fragmented input.
- [x] Implement offer/accept/reject and one streamed file payload, with backpressure.
- [x] Write exclusively inside the selected receive directory; prevent symlink escapes and overwrites.
- [x] Verify SHA-256 using a cryptographic library; report success only after verification.
- [x] Handle interruption, timeout, rejection, invalid metadata, checksum mismatch, and disk errors.
- [x] Add localhost integration tests for success and failure, without public internet access.
- [ ] Verify on Linux before expanding the product (macOS localhost verified; Linux CI configured).

## Later

Follow the phases in `AGENTS.md`: protocol refinement, mDNS discovery, persistent
identity/pairing, text, optional Tailscale endpoints, desktop integration, then Android.
