# Beam

A CLI-first, cross-platform peer-to-peer sharing tool built with C++23 and CMake.
The first product milestone is one verified file transfer over a direct QUIC connection.

This repository currently provides the project scaffold: a reusable `beam-core`, a CLI
with help/version commands, portable filename validation, tests, and CI configuration.
**Networking and file transfer are not implemented yet.** No external runtime libraries
are required for this scaffold.

## Build and test

Requirements: CMake 3.25+, a C++23-capable compiler and its standard library, and a
native build tool. `make check` additionally needs Make and `clang-format`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/beam --help
./build/beam --version
```

With a multi-configuration generator (such as Visual Studio), build with
`--config Debug`, test with `--build-config Debug`, and run `build/Debug/beam.exe`.

Convenience commands on macOS/Linux:

```sh
make build
make test
make check       # formatting check, build, tests
make format      # apply clang-format
make build BUILD_DIR=build-release BUILD_TYPE=Release
```

Install the CLI into a chosen prefix:

```sh
cmake --install build --prefix ./install
```

The allowlist `.gitignore` deliberately excludes build output and local configuration.
Add explicit allowlist entries when introducing new source types or directories.

## Next milestone

The planned commands are:

```sh
beam receive --listen 4269 --output ./received
beam send 192.168.1.50:4269 hello.txt
```

They currently fail with an explicit unsupported-feature error. The transfer milestone
will also require a defined certificate and peer verification configuration.
See [architecture](docs/architecture.md) and [roadmap](docs/ROADMAP.md) for the
implementation boundaries and completion criteria. CI is configured for macOS,
Linux, and Windows; platform validation requires those jobs to run.
