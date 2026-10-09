# Agent Guide

Beam is a CLI-first peer-to-peer sharing tool built with C++23 and CMake.
The current priority is reliable, verified single-file transfers over direct QUIC
on macOS/Linux. Keep future features behind the milestones in [the roadmap](docs/ROADMAP.md).

## Workflow

- Read the relevant documentation below before changing a component.
- Keep changes scoped; extend existing components instead of creating parallel systems.
- Keep common commands in the root Makefile and use out-of-tree CMake builds.
- Use an allowlist `.gitignore`; keep build output, credentials, and local data untracked.
- Use `clang-format`. Run `make check` before completing code changes and add focused
  tests for protocol, state-machine, or input-validation changes.
- Explain new dependencies. Avoid speculative abstractions and measure before optimizing.
- Commit completed tasks with a descriptive message. If a remote is configured, push
  the commit without staging unrelated changes.

## Design and safety

- Preserve `CLI/UI -> core -> protocol/transfer -> transport` dependencies. Keep
  MsQuic handles and platform-specific UI code behind their existing boundaries.
- Use RAII and explicit ownership; keep callback contexts alive through teardown.
  Never detach threads or let exceptions escape C callbacks.
- Follow the mandatory [security rules](docs/security.md): verify peers, validate
  remote input, protect the receive directory, and never execute received content.
- Stream large files with bounded buffers and verify integrity before reporting success.
- Handle failure and cleanup as part of the feature, not as follow-up work.

## Documentation map

| Topic | Read |
| --- | --- |
| Setup, build commands, CLI usage, platform support | [README](README.md) |
| Component boundaries, transport, resource lifetimes | [Architecture](docs/architecture.md) |
| Wire format and transfer state transitions | [Protocol](docs/protocol.md) |
| Certificate provisioning and peer authorization | [TLS setup](docs/tls.md) |
| LAN discovery, advertisement, daemon dependencies | [Discovery](docs/discovery.md) |
| Input, filesystem, credentials, content safety | [Security](docs/security.md) |
| C++ style, concurrency, errors, testing, completion criteria | [Development](docs/development.md) |
| Current status, product scope, future phases | [Roadmap](docs/ROADMAP.md) |

Keep detailed guidance in these documents and update it when behavior changes.
