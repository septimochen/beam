# Architecture

## Current scaffold

`beam` is a thin executable linked to `Beam::Core` (`beam-core`). The core contains
version information and a conservative filename validator, with no UI, OS, or networking
dependencies. Tests use CTest and a small standalone C++ executable; no test framework
dependency is needed at this scale. Tests remain active in Release builds.

The filename validator rejects paths, control bytes, Windows device names, hidden names,
leading spaces, trailing dots/spaces, and names longer than 255 bytes. It currently accepts printable ASCII
only. It does not write files or guarantee filesystem containment. The future receiver
must separately enforce receive-directory containment, avoid following symlinks, create
files exclusively, and handle case-insensitive collisions without overwriting files.

## Transfer milestone

Keep the dependency direction `CLI -> core -> protocol/transfer -> transport`.
Add components only as the direct transfer requires them:

- An internal transport boundary wrapping MsQuic handles and callback lifetimes with RAII.
- A bounded, versioned control codec for metadata and acceptance/results.
- One unidirectional QUIC stream for the file, using bounded read/write buffers.
- SHA-256 verification through an established cryptographic library.
- Explicit server certificate and peer verification configuration before any network use.

MsQuic is the preferred QUIC backend. Choose and document a pinned dependency and
supported platform build strategy when implementing it; the scaffold does not download
dependencies during configuration. Do not disable verification for local development.

The wire format is not defined yet. Freeze it with encoding, decoding, malformed-input,
and stream-fragmentation tests during the transfer implementation. Keep bulk file data
out of the control stream and propagate transport failures into useful CLI errors.
