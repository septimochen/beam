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
