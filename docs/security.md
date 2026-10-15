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
- Treat Tailscale status names, online state, and numeric ranges as endpoint hints.
  Preserve pinned TLS identity on every route. Retry only backend-marked connection
  failures before authentication; never replay an offer or payload after failure.
  Bound local CLI output and lifetime, and reap children on cancellation/error.
  See [Tailscale](tailscale.md).
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
- Desktop clipboard reads require a Load action; sending requires a separate Send
  action to a paired device. Never watch, automatically send, or replace clipboard
  contents. Validate single local file drops and keep notifications free of payload
  text. See [desktop privacy](desktop.md#clipboard-privacy).
- Validate bounded UTF-8 text and single HTTP(S) URLs before sending and again before
  receive publication. Use locally generated storage names for typed content; never
  infer its type from a filename. Do not print payloads in progress or result logs.
  See [content](content.md) for supported syntax and limits.
- Keep private keys and authentication secrets out of logs and source control. Do not
  log clipboard contents by default or full transferred file contents.

## Android boundaries

- Share intents and document-provider names/sizes are untrusted. Accept one
  `content://` URI or bounded text for review; sending requires a separate action
  and a paired-device choice. Never accept filesystem/network URIs or derive a
  filesystem path from a URI. Revalidate the outgoing basename and actual copied bytes.
- Stage provider files in a unique private directory with bounded buffers and a
  4 GiB file limit. Cancel provider reads and native work on Stop; remove staged
  files after the native worker finishes and remove abandoned staging on startup.
- Keep identity, trust, and received content in app-private storage with backup
  disabled. Public certificate export must never expose the private identity file.
  Only an explicit document-picker Save action exports received content.
- Start reception explicitly and show a foreground notification with a Stop
  action. Do not restart after process death or boot. Stop active workers before
  editing/revoking trust. Never open received files/URLs or touch the clipboard.
- The transfer service is unexported. Share-target input cannot invoke native
  commands or start transfers. Catch exceptions at every JNI entry point, bound
  argument/event arrays, and keep native worker ownership behind the value bridge.
