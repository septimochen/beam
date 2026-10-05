# AGENTS.md

## Repository Workflow

- Use C++23 and CMake for the core and CLI. Keep common build, test, check,
  and formatting commands in the root Makefile.
- Use an allowlist `.gitignore`: ignore everything, then explicitly allow
  source files and project configuration. Keep build artifacts untracked.
- Run `make check` before completing code changes. Add focused tests for
  protocol, state-machine, or input-validation changes.
- Commit completed tasks with a descriptive message. If a Git remote is
  configured, push the completed commit without staging unrelated changes.
- If Python tooling is introduced, manage it with `uv` and include `ruff`,
  `ty`, and `pytest` as development dependencies.

## Project Overview

This project is a cross-platform peer-to-peer sharing tool, tentatively named **Beam**.

The goal is to provide fast, simple transfer of:

- files
- images
- music
- clipboard text
- URLs
- other small content payloads

between personal devices running:

- Linux
- macOS
- Windows
- Android

The project should feel closer to a universal AirDrop / clipboard bridge than to a file synchronization system such as `rsync`.

The initial implementation should be CLI-first and focus on correctness, portability, and a clean reusable core.

---

## Core Goals

Prioritize these goals in order:

1. Reliable direct peer-to-peer transfer.
2. Simple UX.
3. Cross-platform portability.
4. Secure device-to-device communication.
5. Clean separation between transport, protocol, application logic, and UI.
6. Good support for both LAN and Tailscale-connected peers.
7. Minimal dependencies and operational complexity.

Do not prioritize advanced synchronization, cloud infrastructure, account systems, or elaborate GUIs until the basic transfer flow is solid.

---

# Technology Choices

## Language

Use:

- C++23
- CMake

Prefer modern C++ idioms and standard library facilities when practical.

Avoid unnecessary third-party abstractions when the standard library is sufficient.

---

## QUIC

Use QUIC as the primary transport.

Preferred implementation:

1. MsQuic
2. quiche if portability or platform support requires it
3. ngtcp2 only if lower-level protocol control becomes necessary

Do not implement QUIC manually.

Do not implement custom TLS.

Do not disable QUIC encryption because traffic may also travel through Tailscale.

QUIC transport details should be hidden behind an internal transport abstraction so changing QUIC libraries does not require rewriting the application.

Example:

```cpp
class TransportConnection {
public:
    virtual ~TransportConnection() = default;

    virtual void send_control_message(
        std::span<const std::byte> data
    ) = 0;

    virtual std::unique_ptr<TransportStream>
    open_stream() = 0;
};
```

Application code should not depend directly on MsQuic handles.

---

# Architecture

The project should follow this rough layering:

```text
CLI / Native UI
       |
       v
   beam-core
       |
       +----------------+
       |                |
       v                v
   protocol         discovery
       |
       v
   transport
       |
       v
      QUIC
       |
       v
      UDP
```

Keep dependencies flowing downward.

Higher layers may depend on lower layers.

Lower layers must not depend on UI or CLI code.

---

# Repository Layout

Prefer a structure similar to:

```text
beam/
├── AGENTS.md
├── CMakeLists.txt
├── cmake/
├── include/
│   └── beam/
│
├── src/
│   ├── core/
│   │   ├── beam.cpp
│   │   ├── device.cpp
│   │   └── transfer.cpp
│   │
│   ├── transport/
│   │   ├── quic_connection.cpp
│   │   ├── quic_listener.cpp
│   │   ├── quic_stream.cpp
│   │   └── msquic/
│   │
│   ├── protocol/
│   │   ├── codec.cpp
│   │   ├── message.cpp
│   │   └── messages/
│   │
│   ├── discovery/
│   │   └── mdns.cpp
│   │
│   ├── transfer/
│   │   ├── sender.cpp
│   │   ├── receiver.cpp
│   │   └── checksum.cpp
│   │
│   └── cli/
│       └── main.cpp
│
├── tests/
│
├── apps/
│   ├── macos/
│   ├── windows/
│   ├── linux/
│   └── android/
│
└── docs/
    ├── protocol.md
    └── architecture.md
```

Do not create all directories prematurely.

Only add structure when functionality requires it.

---

# beam-core

`beam-core` should contain platform-independent application logic.

It should eventually expose APIs approximately like:

```cpp
class Beam {
public:
    std::vector<Device> discover();

    TransferId send_file(
        const Device& device,
        const std::filesystem::path& path
    );

    TransferId send_text(
        const Device& device,
        std::string_view text
    );
};
```

Exact APIs may evolve.

Keep platform-specific clipboard, notification, share-sheet, and GUI logic outside `beam-core`.

---

# First Milestone

The first useful implementation is intentionally small.

Support:

```text
machine A
    |
    | QUIC
    v
machine B
```

Initial flow:

```bash
# receiver
beam receive --listen 4269

# sender
beam send 192.168.1.50:4269 hello.txt
```

The implementation must:

1. establish a QUIC connection
2. send file metadata
3. create a QUIC stream
4. stream file contents
5. write the received file
6. verify integrity
7. report success or failure

Do not add mDNS, GUI, Android, Tailscale discovery, clipboard integration, or history before this path works reliably.

---

# Protocol Design

Use a small application-level protocol directly over QUIC.

Do not introduce HTTP/3 unless a real requirement appears.

Prefer one long-lived QUIC connection between trusted peers.

A connection may contain:

```text
QUIC connection
│
├── control stream
│
├── file stream
│
├── file stream
│
└── text stream
```

Use a dedicated control stream for transfer metadata and coordination.

Possible message types:

```text
HELLO
DEVICE_INFO

TRANSFER_OFFER
TRANSFER_ACCEPT
TRANSFER_REJECT
TRANSFER_CANCEL

TRANSFER_STARTED
TRANSFER_COMPLETE
TRANSFER_FAILED
```

Payload streams should be independent QUIC streams.

Do not multiplex large payloads manually inside the control stream.

---

# Message Encoding

Start simple.

A message should have an explicit version and type.

Example conceptual layout:

```text
version
message_type
payload_length
payload
```

Avoid premature use of complicated serialization frameworks.

Suitable options include:

- simple custom binary encoding
- CBOR
- MessagePack
- Protobuf

Do not choose one solely because it is popular.

Prefer the simplest format that gives:

- clear schema evolution
- bounds checking
- deterministic parsing
- good cross-platform support

All parsing must treat remote input as untrusted.

Never trust lengths, filenames, MIME types, paths, or metadata received from a peer.

---

# File Transfer

Files should normally use one QUIC unidirectional stream per payload.

Metadata belongs in the control protocol.

Example:

```text
TRANSFER_OFFER

id
filename
size
mime_type
checksum
```

After acceptance:

```text
open QUIC stream
send transfer id
send bytes
close stream
```

Large files must be streamed.

Do not load entire files into memory.

Prefer bounded buffers.

---

# Integrity

Use a well-established cryptographic hash for file verification.

Suitable default:

```text
SHA-256
```

Do not implement hashing primitives manually.

Integrity checking is separate from transport encryption.

---

# Device Discovery

LAN discovery should eventually use mDNS.

Example service:

```text
_beam._udp.local
```

or another appropriate service type.

Discovery records should expose only what is necessary to establish a connection.

Possible data:

```text
device name
protocol version
QUIC port
device id
```

Do not advertise secrets.

Discovery is convenience, not authentication.

---

# Tailscale

Beam should work over Tailscale without requiring a special transport protocol.

Conceptually:

```text
Beam
  |
QUIC
  |
UDP
  |
Tailscale
  |
WireGuard
```

Tailscale addresses should be treated as normal network endpoints.

A device may have multiple reachable endpoints:

```cpp
struct Endpoint {
    Address address;
    EndpointKind kind;
};
```

Possible endpoint kinds:

```text
LAN
TAILSCALE
OTHER
```

Prefer direct LAN endpoints when available.

Fall back to Tailscale endpoints when appropriate.

Do not make the main protocol dependent on Tailscale.

Tailscale support should remain optional.

---

# Device Identity

Every Beam installation should eventually have a persistent device identity.

Do not use IP addresses as identity.

A device identity should survive network changes.

Possible concept:

```text
device id
device public key
device display name
```

The device public key should participate in pairing and peer verification.

---

# Pairing

Do not automatically trust every discovered machine.

A future pairing flow may use:

- QR codes
- short authentication codes
- explicit fingerprint comparison

After successful pairing, store the trusted peer identity locally.

Do not create custom cryptographic protocols.

Use established cryptographic libraries.

---

# Security Rules

These rules are mandatory.

Never:

- implement custom encryption
- implement custom TLS
- disable certificate or identity verification for convenience
- trust filenames supplied by peers
- allow arbitrary remote paths
- write outside an explicitly selected receive directory
- execute received content
- interpret received shell commands automatically
- expose clipboard contents without explicit design consideration

Incoming filenames must be sanitized.

For example:

```text
../../.ssh/authorized_keys
```

must never escape the receive directory.

Always assume peer metadata is malicious until validated.

---

# Clipboard Handling

Clipboard support is a later feature.

Represent clipboard transfer as content, not as a file hack.

Possible types:

```text
text/plain
text/uri-list
```

Do not silently execute URLs or commands received from another device.

A received URL may be offered to the user.

A received command should remain plain text.

---

# Async and Concurrency Model

Networking is asynchronous.

Make ownership explicit.

Prefer:

```text
unique_ptr
value types
RAII
```

Use `shared_ptr` only when actual shared ownership exists.

Avoid converting every network object into `shared_ptr`.

Do not detach threads.

Prefer:

- `std::jthread`
- `std::stop_token`
- explicit cancellation
- scoped lifetime management

Transport callbacks must never access destroyed application objects.

Pay close attention to callback lifetime when wrapping MsQuic.

---

# RAII

All native resources should be wrapped.

Examples include:

```text
MsQuic registration
configuration
listener
connection
stream
file handles
threads
sockets
```

The public C++ code should not require callers to manually release transport resources.

Example style:

```cpp
class QuicStream {
public:
    ~QuicStream();

    QuicStream(const QuicStream&) = delete;
    QuicStream& operator=(const QuicStream&) = delete;

    QuicStream(QuicStream&&) noexcept;
    QuicStream& operator=(QuicStream&&) noexcept;
};
```

---

# Error Handling

Do not ignore errors.

Prefer typed errors where practical.

Avoid throwing exceptions through C callbacks.

Translate transport errors at boundaries.

Application errors should preserve useful context.

Prefer messages such as:

```text
failed to connect to Pixel:
QUIC handshake timed out
```

instead of:

```text
error -42
```

---

# Logging

Logging should provide enough information to debug transfers.

Useful fields include:

```text
peer
connection id
transfer id
stream id
bytes sent
bytes received
state transition
error
```

Do not log:

- private keys
- authentication secrets
- clipboard contents by default
- full contents of transferred files

---

# Testing

Unit tests should cover:

- protocol encoding
- protocol decoding
- malformed messages
- filename sanitization
- transfer state transitions
- checksum verification
- endpoint selection

Integration tests should eventually cover:

```text
sender
   |
QUIC localhost
   |
receiver
```

A useful integration test should:

1. generate a temporary file
2. send it
3. receive it into a temporary directory
4. compare bytes
5. verify checksum
6. clean up

Avoid tests that depend on public internet connectivity.

---

# Build

Keep builds reproducible through CMake.

Expected workflow should eventually resemble:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Use out-of-tree builds.

Do not assume an IDE.

---

# Formatting and Style

Prefer a consistent formatter.

Use:

```text
clang-format
```

Avoid unusual formatting rules unless necessary.

Use descriptive names.

Prefer:

```cpp
send_file()
transfer_id
peer_endpoint
```

over:

```cpp
doIt()
x
tmp2
```

Keep functions focused.

Avoid large god classes.

---

# Modern C++ Guidance

Prefer:

```text
std::span
std::string_view
std::filesystem
std::chrono
std::optional
std::expected when available and appropriate
std::jthread
std::stop_token
RAII
```

Avoid unnecessary raw owning pointers.

Raw pointers may be used for non-owning references when clear.

Avoid macros unless required by platform APIs or build configuration.

---

# Platform-Specific Code

Keep native platform code isolated.

Examples:

```text
macOS
    Swift / Objective-C++

Windows
    C++ / WinUI

Linux
    C++ / desktop integration

Android
    Kotlin + JNI
```

The platform UI layer should call into `beam-core`.

Do not copy core transfer logic into each platform application.

---

# Android

Android support should come after the desktop CLI core works.

Prefer:

```text
Kotlin UI
   |
 JNI
   |
beam-core
```

The Android application may eventually support:

- Android share target
- file picker
- receive notifications
- send to known devices
- clipboard actions

Do not start the project by solving Android packaging.

---

# UX Principles

The primary experience should remain simple.

Desired CLI:

```bash
beam devices

beam send photo.jpg pixel

beam send music.flac macbook

beam text "hello" laptop
```

Eventually:

```bash
beam clipboard pixel
```

Users should not normally need to know:

- IP addresses
- QUIC stream IDs
- ports
- certificates
- routing details

These belong below the UX layer.

---

# Non-Goals

Do not turn Beam into the following unless explicitly required later:

- Dropbox replacement
- full folder synchronization engine
- backup system
- distributed filesystem
- messaging platform
- cloud storage provider
- rsync replacement
- remote shell
- arbitrary command execution service

The initial product is an ad-hoc content sharing system.

---

# Avoid Overengineering

Agents working in this repository should strongly prefer the smallest implementation that proves the current milestone.

Do not add abstractions for hypothetical future requirements.

Bad:

```text
GenericPluggableDistributedTransferOrchestrationManager
```

Better:

```text
TransferManager
```

Do not create interfaces until there are either:

- multiple concrete implementations
- a clear testability benefit
- a known architectural boundary

Transport abstraction is an exception because swapping QUIC implementations is a realistic requirement.

---

# Development Sequence

Follow approximately this order.

## Phase 1

Direct file transfer over QUIC.

```text
known IP
known port
one file
CLI only
```

## Phase 2

Protocol cleanup.

Add:

```text
transfer ids
metadata
checksums
progress
cancellation
errors
```

## Phase 3

LAN discovery.

Add mDNS.

```bash
beam devices
```

## Phase 4

Trusted devices.

Add persistent identity and pairing.

## Phase 5

Text transfer.

```bash
beam text "hello" macbook
```

## Phase 6

Tailscale.

Support discovered or configured Tailscale endpoints.

Implement sensible endpoint preference.

## Phase 7

Desktop integration.

Add:

```text
system tray
drag-and-drop
notifications
clipboard integration
```

## Phase 8

Android.

Add:

```text
share target
receiver
device picker
notifications
```

---

# Agent Behavior

When making changes:

1. Read the existing architecture before introducing new abstractions.
2. Keep changes scoped to the requested feature.
3. Prefer modifying existing components over creating parallel systems.
4. Preserve the separation between core logic and UI.
5. Keep transport-specific code isolated.
6. Add tests for protocol or state-machine changes.
7. Do not silently weaken security for development convenience.
8. Do not introduce cloud services unless explicitly requested.
9. Do not add dependencies without explaining why they are necessary.
10. Do not optimize before measuring.

When uncertain, choose the implementation that is:

```text
simpler
safer
easier to test
easier to replace
```

---

# Definition of Done

A feature is not complete merely because it compiles.

It should:

- build on supported target platforms where applicable
- handle failure cases
- clean up resources
- have appropriate tests
- avoid data loss
- avoid obvious path traversal or input validation issues
- produce useful errors
- preserve architecture boundaries

For transfer features specifically, test both:

```text
successful transfer
```

and:

```text
interrupted / rejected / invalid transfer
```

---

# Current Priority

Until explicitly changed, the highest priority is:

```text
Mac/Linux CLI
      |
      v
direct QUIC connection
      |
      v
single file transfer
      |
      v
verified received file
```

Everything else is secondary.

Do not implement the future product before the first reliable transfer works.
