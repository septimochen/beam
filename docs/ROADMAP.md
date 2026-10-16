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
protocol v3; see [protocol](protocol.md) for compatibility and cancellation semantics.

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
| 9 (proposed) | Easier pairing: phone QR scan, desktop nearby-device approval, and invitation fallback; pending review |

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

## Phase 5 implementation

- [x] Explicit file, UTF-8 text/plain, and single HTTP(S) text/uri-list content kinds.
- [x] Paired `beam text TEXT PEER` / `beam url URL PEER` and reusable core APIs.
- [x] Bound text/URL sizes and validate UTF-8, controls, URL syntax, and metadata.
- [x] Reuse authenticated QUIC, SHA-256, progress, cancellation, and acknowledgement.
- [x] Validate received content before safe publication under locally generated names.
- [x] Preserve file-only API rejection and existing paired/dedicated-CA file workflows.
- [x] Test Unicode/empty/boundary/fragmented payloads, malformed input, identity routing,
  integrity/size errors, collisions, cancellation, callbacks, and late-completion output.
- [x] Upgrade ALPN/control/discovery to v3; retain identity and pairing storage.
- [x] Verify macOS Debug/Release, ASan/UBSan, direct-only, discovery-only, scaffold,
  and live Bonjour discovery/transfer checks.
- [x] Verify Phase 5 on Linux CI ([run 38132639495](https://github.com/septimochen/beam/actions/runs/38132639495)),
  including formatting, LLVM 23 static analysis, typed/paired/file transfers,
  and live Avahi discovery.

## Phase 6 implementation

- [x] Optional read-only local Tailscale CLI discovery with numeric address hints.
- [x] Bounded JSON/process handling, deadlines, cancellation, and child cleanup.
- [x] Save a Tailscale route beside a paired endpoint without changing trust.
- [x] Normalize/deduplicate routes; prefer direct, support explicit route selection.
- [x] Fall back only for connectivity failures before authentication/application data.
- [x] Preserve pinned identity and prevent retries after rejection or payload transmission.
- [x] Test route storage/selection, CLI discovery, bounded failures, and localhost fallback.
- [x] Verify macOS Debug/Release, ASan/UBSan, direct-only, Tailscale-only discovery,
  discovery-only, scaffold, and live Bonjour discovery/transfer checks.
- [x] Verify Phase 6 on Linux CI ([run 38135083076](https://github.com/septimochen/beam/actions/runs/38135083076)),
  including formatting, LLVM 23 static analysis, Tailscale fixtures, authenticated
  endpoint fallback, typed/paired/file transfers, and live Avahi discovery.

Beam 0.6.0 retains wire protocol v3 and reads legacy peer records. Route updates
write versioned records that older Beam binaries cannot read. A live Tailscale
tailnet transfer remains unverified; deterministic tests need no account or daemon.
See [Tailscale](tailscale.md) for configuration and safety boundaries.

## Phase 7 implementation

- [x] Optional native SwiftUI/AppKit macOS app and Qt 6 Widgets Linux tray app,
  isolated from the reusable core through one shared desktop service.
- [x] Paired device selection, local single-file drop/selection, and explicit Send.
- [x] GUI-thread clipboard snapshot/review and typed text/URL sharing with byte bounds.
- [x] Explicit receive folder/start/stop, repeated verified transfers, and fresh trust snapshots.
- [x] Progress, send cancellation, bounded worker events, and joined shutdown.
- [x] Tray open/quit and verified completion/error notifications with visible window results.
- [x] Keep received content saved; never open, execute, or replace the clipboard automatically.
- [x] Authenticated controller tests, Swift model/bridge tests on macOS, and
  offscreen drop/clipboard/UI privacy tests on Linux.
- [x] Optional build/Make commands, macOS app bundle, Linux executable, and CI coverage.
- [x] Verify macOS Debug/Release, ASan/UBSan, default CLI, direct-only desktop,
  scaffold, local static analysis, and live Bonjour desktop advertisement/transfers.
- [x] Verify Phase 7 on Linux CI ([run 38138107214](https://github.com/septimochen/beam/actions/runs/38138107214)),
  including formatting, LLVM 23 static analysis, authenticated desktop worker
  transfers, offscreen UI privacy checks, and live Avahi advertisement/withdrawal.

The SwiftUI macOS frontend reuses the same C++ service through an Objective-C++
bridge. Automated native tests cover paired file/text/URL transfers, exact clipboard
snapshots, local drop validation, close-to-menu-bar policy, and worker shutdown.
Interactive window/accessibility inspection, menu bar gestures, and OS notification
delivery still require a desktop session check; automated tests cannot confirm them.

Beam 0.7.0 retains protocol v3 and existing identity/route records. See
[desktop](desktop.md) for build and launch instructions, the explicit clipboard
privacy design, and native desktop verification limits.

## Phase 8 implementation

- [x] Android Kotlin app and bounded JNI value bridge reusing paired C++ core/workers.
- [x] Single-item share target and system file picker with separate review and Send.
- [x] Paired-device picker, public certificate import/export, reciprocal fingerprint
  approval, address updates, and explicit revocation.
- [x] Private streamed URI staging, actual byte bounds, progress/cancellation,
  and cleanup after verified completion or failure.
- [x] Explicit repeated reception in a foreground service with Stop controls;
  safe private publication and explicit document-picker export.
- [x] Preserve pinning, SHA-256, acknowledgement, and protocol v3 interoperability;
  Android sandbox storage uses no-replace rename and private trust snapshots.
- [x] Pinned ARM64/x86-64 native builds, checksum-verified Gradle wrapper, SDK 37
  local-network permission flow, backup exclusion, Make commands, and Android CI.
- [x] Native session/input tests, provider/JVM tests, and actual Android 15/17
  emulator JNI file/text/URL, foreground/provider, and share-review tests.
- [x] macOS Debug `make check`, Release core/desktop suite, ASan/UBSan,
  scaffold-only build, Android Kotlin formatting/lint/unit tests, and both-ABI APK.

Beam 0.8.0 retains wire protocol v3 and existing identity/route records. Android
uses saved numeric endpoints; LAN discovery/advertisement and Tailscale CLI
integration remain desktop/CLI features. Physical-device LAN/tailnet behavior,
OEM background policy, and third-party provider compatibility remain unverified.
See [Android](android.md) for build, install, pairing, and validation details.

## Phase 9 proposal: easier pairing (pending review)

This is a design proposal for later review and implementation. The suggested
combination is QR pairing for phones, nearby-device selection for macOS/Linux,
and a copy/paste invitation fallback. The desktop flow and authentication protocol
remain open decisions; adding this proposal does not start implementation.

- [ ] Phone QR flow: computer opens **Add device → Show QR code**, Android opens
  **Add device → Scan QR code**, and the computer approves the phone. Exchange
  public certificates and save reciprocal trust through the authenticated pairing
  session, without manual PEM files, fingerprint transcription, or IP entry.
- [ ] Define a temporary QR invitation containing connection details, the host's
  certificate fingerprint, and a secret valid for one pairing session. Expire and
  invalidate invitations after use or cancellation; never advertise secrets in
  discovery records or expose them in logs.
- [ ] Proposed desktop flow: open **Add device** on both computers, select the
  nearby device, compare a short verification code, and confirm on both. Discovery
  supplies untrusted connection hints and cannot authorize a peer.
- [ ] Provide copy/paste invitations as a fallback when discovery is unavailable,
  including a CLI entry point that reuses the shared pairing service.
- [ ] Review typed pairing codes as an alternative desktop/terminal flow before
  choosing the final UX. Short codes require an established authenticated pairing
  protocol and attempt limits; do not invent cryptography.
- [ ] Review and document the authentication design in [pairing](pairing.md) and
  [security](security.md) before implementation. Require explicit approval, prove
  possession of the paired keys, retain exact certificate pinning for transfers,
  and keep private keys on their original devices. Preserve manual PEM pairing
  as an advanced option.
- [ ] Validate expiration, replay, wrong codes, mismatched identities, rejection,
  interrupted reciprocal approval, and recovery without silently granting trust.
  Verify Android/macOS/Linux interoperability and clear completion/error states.

## Product scope

Beam is an ad-hoc sharing tool for files, images, music, clipboard text, URLs, and
other small payloads across personal Linux, macOS, Windows, and Android devices.
Favor reliable transfer, simple UX, portability, security, clear component boundaries,
LAN/Tailscale reachability, and minimal dependencies or operational complexity.

Future everyday commands should select known devices by name, such as
`beam send photo.jpg pixel` or `beam clipboard laptop`. Ports, routing, stream IDs,
and certificate details should eventually live below that UX. Paired aliases now
support file, text, and URL transfers with stored direct/Tailscale routes or an
explicit endpoint override. Discovery remains an untrusted address hint;
see [pairing](pairing.md) for identity and trust management.

Do not add cloud infrastructure or accounts unless explicitly requested. Full folder
synchronization, backup, distributed filesystems, cloud storage, messaging, and remote
shells are outside the product scope unless a later user request changes it.
