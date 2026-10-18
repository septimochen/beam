# Architecture

Dependency flow is `CLI/UI -> beam-core -> protocol/transfer -> transport -> MsQuic`.
The CLI parses arguments and reports outcomes; application code sees no MsQuic handles.
The core exposes file/text sending, `receive_content`, file-only `receive_file`,
credentials, endpoints, and typed transfer results.
Typed transfer errors carry a category and peer/local origin across the CLI boundary; C callbacks catch exceptions locally.

`src/transport/transport.hpp` is the internal QUIC boundary. Its connection and stream
interfaces let the core use blocking operations while MsQuic performs asynchronous
networking on its worker threads. RAII owns the API table, registration, configuration,
listener, connection, and stream handles. Teardown shuts streams/connections down,
waits for completion, then closes handles while callback contexts remain alive.
A separate OpenSSL X.509 store containing only `--ca` verifies the portable peer
certificate chain, in addition to MsQuic TLS verification: MsQuic also loads system
roots, which must not grant Beam peer authorization. In paired mode, the runtime
loads an immutable bundle of approved self-signed certificates and checks the
exact SHA-256 leaf fingerprint as well as normal TLS chain/hostname verification. No detached application threads or shared ownership are needed.

A stream send waits for buffer release with MsQuic send buffering disabled, preventing
unbounded queued file data. Receive callbacks retain a single pending MsQuic event;
application reads consume it before `StreamReceiveComplete` permits more delivery.
Zero-byte FIN receive events complete inline. Cancellation uses a caller-owned stop token
or calling-thread predicate, checked between buffers and every 50 ms during waits.
Failures close with a bounded application error code, including cancellation, so a
blocked peer payload operation sees the reason without an extra control reader thread.
Progress callbacks run synchronously on the application thread and never in C callbacks. The payload buffer and configured
receive window are 64 KiB. Each wait has a deadline and peer-abort propagation.

`src/protocol` defines bounded versioned frames and transfer IDs. `src/core/transfer.cpp`
implements offer/accept/payload/verified-completion state transitions. `src/transfer`
uses OpenSSL EVP SHA-256 and RAND for established cryptographic primitives, plus POSIX
file operations isolated from protocol/transport. Directory-relative exclusive creation
and no-replace publication protect the selected destination from symlinks, races, and
collisions; incomplete files never occupy their final name.

File and bounded in-memory text sources use the same sending state machine.
`src/transfer/content.cpp` validates bounded UTF-8 text without
new dependencies. Typed offers include a content kind; bounded text payloads
are revalidated after integrity checks and returned in memory after acknowledgement.
The core exposes the kind independently of storage names and never opens content.
See [content](content.md) for bounds and API semantics.

The transfer implementation targets macOS/Linux/Android. Windows still builds the portable filename/core scaffold;
Windows exclusive storage and credential provisioning remain future work. MsQuic v2.6.2
is pinned by the explicit `make deps` workflow. Regular CMake configuration discovers
installed dependencies and never downloads code. See [TLS](tls.md) for peer authorization,
[wire protocol](protocol.md), and the upstream [MsQuic build guide](https://github.com/microsoft/msquic/blob/v2.6.2/docs/BUILD.md).

The macOS application bridge and CLI transfer entry point ignore `SIGPIPE` before
starting network workers. Failed Darwin UDP sends in the pinned MsQuic backend can
otherwise terminate the process before a transport error reaches the caller. This
process-wide signal policy belongs to the applications, not the reusable core;
timeouts, cancellation, and authenticated peer verification still apply normally.

## Design constraints

Keep dependencies flowing downward. Protocol, transfer, discovery, and transport code
must not depend on CLI or native UI code. Keep platform-specific clipboard, notifications,
share sheets, and GUI logic outside the reusable core. Native applications call the core
rather than copying transfer logic; Android uses Kotlin through JNI.
Add platform directories only when implementing their functionality.

The optional desktop app lives in `src/desktop`: shared worker ownership is in
`service.hpp/.cpp`, Linux Qt UI is in `qt/`, and native macOS UI is in `macos/`.
SwiftUI owns macOS controls; AppKit owns the menu bar, window lifecycle, file
dialogs, drops, and explicit clipboard access. A small Objective-C++ bridge
translates C++ exceptions to NSError and drains value snapshots on the main
thread. C++ objects and callbacks never cross into Swift. Both frontends use one
GUI-independent service that
owns send, receive, and connectivity-check `std::jthread`s, with stop-token
cancellation and joining teardown. It calls the existing paired core APIs,
reloads receiver credentials between transfers, and owns advertisements only
while each listener exists. A bounded mailbox coalesces progress; a GUI timer
drains events without crossing widget/clipboard operations onto worker threads.
File drops and explicit clipboard reads only select content for a subsequent
Send action. Success notifications follow the returned verified transfer result.
See [desktop](desktop.md) for the clipboard privacy design and platform limits.

The Android app in `android/` uses platform Kotlin widgets and a bound/started
foreground service. `src/android/session` translates bounded value commands into
IdentityStore and the existing `src/desktop/service` worker owner; both desktop
and Android link `beam-app-support`. The JNI shim catches C++ exceptions and
passes UTF-8 byte arrays, including supplementary Unicode, without modified-UTF-8
conversion. Workers never retain JNI references or call Java. A main-thread poll
drains the existing bounded mailbox; destroying the native session cancels and joins
workers. Android document providers are read through a private streamed staging
file, keeping content URIs and platform APIs out of the transfer core. Android
builds statically link the pinned MsQuic/quictls into one JNI library and package
NDK libc++; they disable desktop DNS-SD and local Tailscale subprocess discovery.
See [Android](android.md) for lifecycle, storage, and build requirements.

QUIC is the primary transport. Prefer MsQuic; consider quiche only when platform support
requires it, and ngtcp2 only when lower-level control is necessary. Hide backend details
behind the internal transport boundary. Do not implement QUIC or TLS manually, and do not
introduce HTTP/3 without a concrete requirement.

Keep the application protocol small, versioned, deterministic, and bounded. Metadata and
coordination belong on a dedicated control stream; file payloads normally use independent
unidirectional streams. Do not manually multiplex bulk payloads into the control stream.
The current format is described in [protocol](protocol.md); evolve that format instead of
adding a serialization framework solely for popularity.

Future trusted peers may reuse long-lived connections, but the current implementation deliberately uses one
connection per transfer. LAN discovery uses mDNS with minimal connection metadata.
Tailscale addresses remain ordinary endpoints: prefer saved direct paths when available,
fall back safely to Tailscale, and keep both discovery sources optional.
See [the roadmap](ROADMAP.md) before expanding the current scope.

## Endpoint routing and Tailscale

`routing.hpp` provides pure numeric endpoint validation, normalization, Tailscale
range classification, and selection from a trusted peer's saved routes. The CLI
chooses direct-first candidates or an explicit preference and supplies the same
immutable credentials for each. The shared sending state machine retries only
backend-marked connectivity failures before an authenticated connection exists.
It never replays offers or payloads, including after a final-exchange failure.

`tailscale.hpp` exposes optional local `tailscale status --json` discovery.
`src/discovery/tailscale.cpp` uses bounded nlohmann_json parsing and a scoped POSIX
subprocess with cancellation, a total deadline, and process-group cleanup. It is
independent of certificate storage and transfer state. Names, online state, and
addresses are hints; saving a route remains explicit. Tailscale itself controls
its direct/relayed network path. See [Tailscale](tailscale.md).

## LAN discovery

`include/beam/discovery.hpp` exposes bounded browsing and an RAII receiver
advertisement. `src/discovery/dnssd.cpp` owns Bonjour/Avahi references behind that
boundary; it does not depend on CLI, QUIC, or certificate storage. The CLI starts
advertisement through the existing calling-thread `on_listening` hook and owns it
until the transfer finishes. Core API callers can use the same hook explicitly.

Browsing polls daemon sockets on the calling thread, resolves SRV/TXT and A/AAAA
records, tracks removals and interface-specific endpoints, and checks cancellation
at most every 50 ms between daemon batches. C callbacks retain exceptions for the
application thread. Completed resolve references are closed outside callbacks.
Advertisement registers synchronously with a bounded deadline, then a scoped
`std::jthread` processes daemon events. Destruction joins the worker before releasing
its registration and callback state. Asynchronous registration errors are surfaced
by `check()`; the CLI warns and withdraws the advertisement while preserving transfer.
See [discovery](discovery.md) for wire metadata and trust boundaries.

## Persistent identity and trusted peers

`include/beam/identity.hpp` exposes initialization, public certificate export,
fingerprint-verified pairing, peer lookup, address changes, removal, and credential
snapshots. `src/core/identity.cpp` uses the existing OpenSSL dependency for P-256
keys, self-signed certificates, and SHA-256 fingerprints. POSIX directory-relative
storage, file ownership/permission checks, a process lock, and atomic publication
protect the persistent identity and peer records. No crypto protocol is introduced.

The CLI loads a credential snapshot and releases the store lock before network
operations. A sender selects one peer; a receiver snapshots all currently paired
peers. Runtime-owned temporary CA bundles adapt these immutable snapshots to
MsQuic's existing certificate-file API. Exact leaf pinning prevents a system root,
other paired peer, or a certificate issued by a paired identity from authorizing
an unintended device. Explicit-CA transfers keep their original validation path.
Device identity, local aliases, LAN display names, and numeric routing endpoints
are separate: changing an address never changes trust. See [pairing](pairing.md).

## Seamless LAN pairing

`PairingSession` owns joined pairing/discovery workers and exposes bounded value
snapshots to CLI, Swift/Objective-C++, Qt, and JNI. `protocol/pairing` implements
bounded framing and canonical invitation encoding; `transport/pairing_tls` owns
nonblocking sockets and Mbed TLS contexts. Its EC-JPAKE channel authenticates the
invitation/PIN, followed by fresh pinned mutual TLS 1.3 for certificate-key proof.
The UI approves both identities before IdentityStore prepares and commits trust.
An incomplete journal never participates in credential snapshots. Existing peer
records and the `beam/4` transfer protocol are unchanged. Mbed TLS's PSA backend
uses pthread locking, including when multiple sessions run in one process.

Desktop reuses Bonjour/Avahi for `_beam-pair._tcp`; Android uses NsdManager outside
the C++ core. QR modules come from the vendored MIT Nayuki encoder; Android's
CameraX/ZXing scanner is platform UI and never authorizes trust itself. Pairing
listeners and secrets exist only for the bounded Add device window. No persistent
control connection, cloud rendezvous, relay, or account is introduced.
