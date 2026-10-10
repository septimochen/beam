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
- [x] Verify direct transfers on Linux (Ubuntu CI run [37630803802](https://github.com/septimochen/beam/actions/runs/37630803802)); macOS localhost verified.

## Development phases

Keep this order unless the user explicitly changes priorities. Phase 1 now includes
the metadata, IDs, checksums, and errors required for a reliable transfer; Phase 2
refines that implementation rather than creating a replacement. Both peers now use
protocol v2; see [protocol](protocol.md) for compatibility and cancellation semantics.

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

## Phase 2 implementation

- [x] Versioned machine-readable failure categories and early header validation.
- [x] Calling-thread progress for hashing, payload bytes, verification, and completion.
- [x] CLI progress on stderr and `--no-progress` for scripts.
- [x] Core stop-token cancellation and CLI Ctrl-C, including blocked network waits.
- [x] Propagate cancellation/failure categories to a blocked peer and clean partial files.
- [x] Test malformed failures, wrong responses, cancellation, callback errors, and final-exchange preservation.
- [x] Verify Phase 2 on Linux CI ([run 37775319586](https://github.com/septimochen/beam/actions/runs/37775319586)); macOS Debug, Release, sanitizer, and scaffold-only checks passed.

## Phase 3 implementation

- [x] Optional Bonjour/Avahi DNS-SD backend behind a reusable core API.
- [x] `beam devices` with bounded browsing, numeric IPv4/IPv6 endpoints, and Ctrl-C.
- [x] Advertise bound receivers with names and protocol metadata; allow `--no-discovery`.
- [x] Withdraw advertisements and pending queries on completion, failure, and cancellation.
- [x] Validate untrusted records; retain certificate authentication for discovered endpoints.
- [x] Test records, duplicates/removals, interface scopes, resource bounds, and daemon failures.
- [x] Verify live macOS discovery and a TLS-verified CLI transfer through a discovered endpoint.
- [x] Verify Phase 3 on Linux CI ([run 37936998371](https://github.com/septimochen/beam/actions/runs/37936998371)), including live Avahi discovery and a verified transfer; macOS Debug, Release, sanitizer, scaffold-only, and discovery-only checks passed.

## Phase 4 implementation

- [x] Persistent OpenSSL device certificate/key and stable TLS identity on macOS/Linux.
- [x] Explicit reciprocal pairing after independent full SHA-256 fingerprint verification.
- [x] Owner-only, bounded, locked, atomic identity and trusted-peer storage.
- [x] Peer listing, explicit removal, and address updates without changing trust.
- [x] `beam send FILE PEER` and receivers using stored paired credentials.
- [x] Exact leaf certificate pinning alongside normal TLS validation; retain dedicated-CA mode.
- [x] Test restart/concurrent initialization, storage safety, invalid pairing, authenticated
  name-based transfers, IPv6/address changes, wrong identities, and revocation.
- [x] Verify macOS Debug, Release, ASan/UBSan, direct-only, discovery-only, scaffold,
  and live Bonjour discovery checks.
- [x] Verify Phase 4 on Linux CI ([run 38064001209](https://github.com/septimochen/beam/actions/runs/38064001209)),
  including formatting, static analysis, paired transfers, and live Avahi discovery.

## Product scope

Beam is an ad-hoc sharing tool for files, images, music, clipboard text, URLs, and
other small payloads across personal Linux, macOS, Windows, and Android devices.
Favor reliable transfer, simple UX, portability, security, clear component boundaries,
LAN/Tailscale reachability, and minimal dependencies or operational complexity.

Future everyday commands should select known devices by name, such as
`beam send photo.jpg pixel` or `beam clipboard laptop`. Ports, routing, stream IDs,
and certificate details should eventually live below that UX. Phase 4 now selects
paired receivers by local alias for file sending, with stored
or explicitly overridden endpoints. Discovery remains an untrusted address hint;
see [pairing](pairing.md) for identity and trust management.

Do not add cloud infrastructure or accounts unless explicitly requested. Full folder
synchronization, backup, distributed filesystems, cloud storage, messaging, and remote
shells are outside the product scope unless a later user request changes it.
