# Security Rules

These rules are mandatory for all Beam components. Read [TLS setup](tls.md) for the
current certificate authorization model and [protocol](protocol.md) for wire validation.

## Peer verification and cryptography

- Use established QUIC, TLS, encryption, and hashing implementations. Never implement
  those primitives or a custom pairing cryptographic protocol.
- Never disable certificate or identity verification for development convenience.
  Keep QUIC encryption enabled even when the connection travels over Tailscale.
- Discovery is a convenience, not authentication. Never automatically trust a
  discovered machine or advertise secrets in discovery records.
- Persistent identity must survive network changes; an IP address is not
  device identity. Pairing must explicitly verify the full certificate fingerprint
  through an independent channel before storing trust. See [pairing](pairing.md).
- Paired transfers must check the exact leaf certificate and normal TLS validity
  and hostname rules. Never authorize another certificate solely because a paired
  certificate or system root signed it. Empty trust sets must fail closed.
- Keep identity keys and peer records in an owner-only store. Reject symlink and
  hardlink records, bound file/peer counts, serialize mutations, and publish them
  atomically. Never silently replace a damaged identity or an existing peer.

## Remote input and files

- Treat all peer input as untrusted, including lengths, filenames, paths, MIME types,
  and metadata. Bound allocations and validate every field before use.
- Reject unsafe incoming filenames. Never allow arbitrary remote paths or writes
  outside an explicitly selected receive directory, including traversal such as
  `../../.ssh/authorized_keys`.
- Create receive files exclusively, prevent symlink escapes and overwrites, and handle
  collisions on case-insensitive filesystems. Failed transfers must not publish partial
  files under their final name; preserve existing user data.
- Stream large files with bounded buffers. Use SHA-256 through an established library
  for integrity verification, separately from transport encryption. Report success only
  after the received size and digest are verified.

## Received content and secrets

- Never execute received content or interpret received shell commands automatically.
  A received command remains plain text; opening a received URL requires user choice.
- Clipboard sharing needs an explicit privacy design. Represent text and URLs as typed
  content (`text/plain`, `text/uri-list`), not disguised files.
- Keep private keys and authentication secrets out of logs and source control. Do not
  log clipboard contents by default or full transferred file contents.
