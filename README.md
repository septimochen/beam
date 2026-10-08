# Beam

A CLI-first peer-to-peer sharing tool built with C++23 and CMake. Phase 2 transfers
one file over a direct, mutually authenticated QUIC connection on macOS/Linux,
reports progress, supports cancellation, streams with bounded buffers, and verifies SHA-256 before publishing the received file.
No discovery, pairing service, cloud infrastructure, or GUI is involved.

## Build and test

Requirements: CMake 3.25+, a C++23 compiler, Git, Perl, Make, OpenSSL development
headers/libraries (1.1.1+), and `clang-format` 23 for `make check`. The pinned MsQuic
build uses its own quictls TLS implementation; Beam uses the installed OpenSSL
libcrypto for hashing and random transfer IDs. These dependencies provide QUIC/TLS
and cryptographic primitives without implementing them ourselves.

On macOS, install the build tools with Homebrew (`cmake`, `openssl@3`, `llvm`) and
run `export CLANG_FORMAT="$(brew --prefix llvm)/bin/clang-format"` (LLVM 23).
On Linux, install the corresponding tools, including `build-essential`, `libssl-dev`,
`perl`, and `cmake` on Debian/Ubuntu. Install `clang-format-23` from the
[official LLVM package repository](https://apt.llvm.org/) for your distribution,
and run `export CLANG_FORMAT=clang-format-23`.

```sh
make deps        # explicit network download/build of MsQuic v2.6.2; takes a few minutes
make check       # formatting, build, unit tests and local QUIC integration
./build/beam --help
```

`make deps` checks commit `819ab74f851ee168504cbc392ec32e7bed1d82e9`, initializes only
its pinned TLS submodule, and installs into ignored `build/deps/install`. Normal CMake
configuration never downloads dependencies. An existing OpenSSL-backed MsQuic installation
can be used instead by passing `-DMSQUIC_ROOT=/path/to/prefix` to CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSQUIC_ROOT="$PWD/build/deps/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
make build BUILD_DIR=build-release BUILD_TYPE=Release
```

Windows still builds the help/version and filename-validation scaffold by default;
Transfer filesystem handling is currently POSIX-only. To build only the scaffold on
any platform, configure with `-DBEAM_ENABLE_QUIC=OFF`.

## Transfer a file

Provision certificates as described in [TLS setup](docs/tls.md). Both certificates
must be signed by a dedicated private CA that authorizes only the participating
devices. The receiver verifies client certificates; the sender verifies the server
certificate chain and its IP address or explicit `--server-name`. There is no insecure mode.

Receiver (create/select a directory first):

```sh
mkdir -p received
./build/beam receive --listen 4269 --output ./received \
  --cert server.pem --key server.key --ca ca.pem
```

Sender:

```sh
./build/beam send 192.168.1.50:4269 hello.txt \
  --cert client.pem --key client.key --ca ca.pem
```

Use `[IPv6]:4269` for IPv6. If the receiver certificate names `my-laptop` rather than
its IP, add `--server-name my-laptop`. `--timeout 120` allows longer individual waits;
the default is 30 seconds. Allow UDP port 4269 through the receiver's firewall.
The receiver accepts one authenticated connection and one file, then exits.

Progress goes to stderr, with byte counts and hashing/transfer/verification stages.
Use `--no-progress` for quiet scripts; verified success remains on stdout. Press Ctrl-C
on either peer to cancel, including while waiting for a connection or stream. Beam
exits with code 130 for local or peer cancellation, 2 for argument errors, and 1 for
other failures. Partial receive files are removed before exit. Network waits check
cancellation every 50 ms; synchronous filesystem operations and user callbacks must
finish before cancellation can proceed.

Both peers must use Phase 2 builds (`beam/2`); Phase 1 peers fail protocol negotiation.
Errors include a category such as `destination exists`, `integrity verification failed`,
or `timed out`. Peer errors contain safe category text rather than remote paths.

Core callers can set `TransferOptions::stop_token`, `on_progress`, and an optional
`should_cancel` predicate. Progress and cancellation predicates run on the calling
thread. Progress counts payload bytes separately from hashing, starts at zero, and
reports `complete` only after verification and acknowledgement. Callback exceptions
abort the transfer and propagate to the caller. `TransferError` provides a category
and a `remote` flag and remains compatible with `std::runtime_error` handlers.

Success means the receiver checked the exact byte count and SHA-256, flushed the file,
and published it without replacing any existing filename. The receiver stores files
with mode 0600. Names must be portable printable ASCII basenames (up to 255 bytes).
Paths, hidden names, reserved Windows names, source symlinks, symlink receive directories,
and destination collisions are rejected. A failed transfer removes its temporary file;
force-killing the receiver or power loss can leave a hidden `.beam-*.part` file.

An interrupted or cancelled final acknowledgement may leave a fully verified file while the sender
reports failure. Check the destination before retrying: Beam refuses to overwrite it.
Inputs should remain unchanged during a transfer; a changed input fails size/hash validation.

## Verification and scope

CTest covers filename validation, CLI arguments, malformed and fragmented protocol
frames, the SHA-256 standard test vector, exclusive receive storage, and actual
localhost QUIC transfers. Integration cases include empty/binary/3 MiB files,
IPv4/IPv6, overwrite rejection, wrong server names, untrusted clients and default trust roots outside `--ca`, checksum/size/ID errors,
invalid metadata/state, interruption cleanup, and listener timeout. Phase 2 adds
progress callback contracts, local/peer cancellation during hashing/streaming/verification,
Ctrl-C during listener/payload/acceptance/acknowledgement waits, callback failure cleanup,
structured error parsing, and deadlines under spurious wakeups. No public network
is required for tests.

Run `make format` to apply formatting. Formatting commands require clang-format 23
to keep local and CI results consistent; `CLANG_FORMAT` can be set in the environment
or passed to Make. The allowlist `.gitignore` keeps builds,
certificates, keys, and local receive directories untracked. Installing `beam` with
`cmake --install build --prefix ./install` requires the MsQuic/OpenSSL shared libraries
to remain available to the runtime loader; this milestone does not bundle an installer.

See [architecture](docs/architecture.md), [protocol](docs/protocol.md), and
[roadmap](docs/ROADMAP.md) for the boundaries and later phases.
