# Roadmap

## Bootstrap

- [x] C++23/CMake core and CLI targets, out-of-tree builds, installation.
- [x] Make build, test, check, and formatting commands; allowlist gitignore.
- [x] Portable filename rejection with malicious-input and boundary tests.
- [x] CLI help/version and explicit errors for unavailable transfers.
- [x] CTest, automatic main-branch checks, and manual macOS/Linux/Windows CI builds.

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

## Development phases

Keep this order unless the user explicitly changes priorities. Phase 1 now includes
the metadata, IDs, checksums, and errors required for a reliable transfer; Phase 2
refines that implementation rather than creating a replacement.

| Phase | Scope |
| --- | --- |
| 1 | macOS/Linux CLI, known IP and port, one direct QUIC file transfer, verified output |
| 2 | Protocol refinement, progress, cancellation, and clearer transfer errors |
| 3 | LAN discovery through mDNS; `beam devices` |
| 4 | Persistent device identity, explicit pairing, and stored trusted peers |
| 5 | Typed text/URL transfer; `beam text "hello" macbook` |
| 6 | Optional Tailscale endpoint configuration/discovery and sensible endpoint preference |
| 7 | Desktop tray, drag-and-drop, notifications, and explicit clipboard sharing |
| 8 | Android Kotlin/JNI app with share target, file picker, receiver, and device picker |

Do not begin discovery, GUI, Android packaging, clipboard integration, or history
before the direct transfer path is reliable on macOS and Linux.

## Product scope

Beam is an ad-hoc sharing tool for files, images, music, clipboard text, URLs, and
other small payloads across personal Linux, macOS, Windows, and Android devices.
Favor reliable transfer, simple UX, portability, security, clear component boundaries,
LAN/Tailscale reachability, and minimal dependencies or operational complexity.

Future everyday commands should select known devices by name, such as
`beam send photo.jpg pixel` or `beam clipboard laptop`. Ports, routing, stream IDs,
and certificate details should eventually live below that UX. The current CLI
intentionally exposes endpoints and credentials to prove the first milestone.

Do not add cloud infrastructure or accounts unless explicitly requested. Full folder
synchronization, backup, distributed filesystems, cloud storage, messaging, and remote
shells are outside the product scope unless a later user request changes it.
