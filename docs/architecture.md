# Architecture

Dependency flow is `CLI -> beam-core -> protocol/transfer -> transport -> MsQuic`.
The CLI parses arguments and reports outcomes; application code sees no MsQuic handles.
The core exposes `send_file`/`receive_file`, credentials, endpoints, and transfer results.
Errors cross the CLI boundary as exceptions; C callbacks catch exceptions locally.

`src/transport/transport.hpp` is the internal QUIC boundary. Its connection and stream
interfaces let the core use blocking operations while MsQuic performs asynchronous
networking on its worker threads. RAII owns the API table, registration, configuration,
listener, connection, and stream handles. Teardown shuts streams/connections down,
waits for completion, then closes handles while callback contexts remain alive.
A separate OpenSSL X.509 store containing only `--ca` verifies the portable peer
certificate chain, in addition to MsQuic TLS verification: MsQuic also loads system
roots, which must not grant Beam peer authorization. No detached application threads or shared ownership are needed.

A stream send waits for buffer release with MsQuic send buffering disabled, preventing
unbounded queued file data. Receive callbacks retain a single pending MsQuic event;
application reads consume it before `StreamReceiveComplete` permits more delivery.
Zero-byte FIN receive events complete inline. The payload buffer and configured
receive window are 64 KiB. Each wait has a deadline and peer-abort propagation.

`src/protocol` defines bounded versioned frames and transfer IDs. `src/core/transfer.cpp`
implements offer/accept/payload/verified-completion state transitions. `src/transfer`
uses OpenSSL EVP SHA-256 and RAND for established cryptographic primitives, plus POSIX
file operations isolated from protocol/transport. Directory-relative exclusive creation
and no-replace publication protect the selected destination from symlinks, races, and
collisions; incomplete files never occupy their final name.

Phase 1 targets macOS/Linux. Windows still builds the portable filename/core scaffold;
Windows exclusive storage and credential provisioning remain future work. MsQuic v2.6.2
is pinned by the explicit `make deps` workflow. Regular CMake configuration discovers
installed dependencies and never downloads code. See [TLS](tls.md) for peer authorization,
[wire protocol](protocol.md), and the upstream [MsQuic build guide](https://github.com/microsoft/msquic/blob/v2.6.2/docs/BUILD.md).

## Design constraints

Keep dependencies flowing downward. Protocol, transfer, discovery, and transport code
must not depend on CLI or native UI code. Keep platform-specific clipboard, notifications,
share sheets, and GUI logic outside the reusable core. Native applications call the core
rather than copying transfer logic; Android should eventually use Kotlin through JNI.
Add platform directories only when implementing their functionality.

QUIC is the primary transport. Prefer MsQuic; consider quiche only when platform support
requires it, and ngtcp2 only when lower-level control is necessary. Hide backend details
behind the internal transport boundary. Do not implement QUIC or TLS manually, and do not
introduce HTTP/3 without a concrete requirement.

Keep the application protocol small, versioned, deterministic, and bounded. Metadata and
coordination belong on a dedicated control stream; file payloads normally use independent
unidirectional streams. Do not manually multiplex bulk payloads into the control stream.
The current format is described in [protocol](protocol.md); evolve that format instead of
adding a serialization framework solely for popularity.

Future trusted peers may reuse long-lived connections, but Phase 1 deliberately uses one
connection per transfer. LAN discovery should use mDNS with minimal connection metadata.
Tailscale addresses remain ordinary endpoints: prefer direct LAN paths when available,
fall back to Tailscale when appropriate, and keep both discovery and Tailscale optional.
See [the roadmap](ROADMAP.md) before implementing those features.
