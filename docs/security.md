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
  device identity. Manual pairing must explicitly verify the full certificate fingerprint
  through an independent channel. Network pairing may instead use the reviewed
  EC-JPAKE exchange, fresh pinned mutual-TLS key proof, and reciprocal approval
  described in pairing.md before storing trust. Never treat a short PIN as a
  raw TLS PSK, skip key proof, or promote pending journal records to trust. See [pairing](pairing.md).
- Paired transfers must check the exact leaf certificate and normal TLS validity
  and hostname rules. Never authorize another certificate solely because a paired
  certificate or system root signed it. Empty trust sets must fail closed.
- Keep identity keys and peer records in an owner-only store. Reject symlink and
  hardlink records, bound file/peer counts, serialize mutations, and publish them
  atomically. Never silently replace a damaged identity or an existing peer.
- App unpairing must cancel and join workers holding trust snapshots before
  removing the peer, discard its matching interrupted-pairing journal, and reload
  trust before restarting reception. It revokes only local trust; no unauthenticated
  remote message can revoke or restore a peer.

- Certificate replacement requires an independently verified full SHA-256 pin;
  reject duplicate/self identities and stale edit snapshots. Native apps join
  workers before edits or import so old trust cannot survive the change.
- Device backups carry public certificates/routes only. Bound and validate the
  entire backup, preview before approval, reject conflicts without partial import,
  and keep existing settings. Never treat the source fingerprint as a signature.
  A backup cannot restore a lost private identity or authorize the new identity
  on remote devices. Replay durable device transactions under the store lock
  before exposing trust after interruption.

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
- Clipboard sharing needs an explicit privacy design. Treat text and URLs as bounded UTF-8 `text/plain`, not disguised files.
- Desktop clipboard reads require a Load action; sending requires a separate Send
  action to a paired device. Never watch, automatically send, or replace clipboard
  contents automatically. Writes require an explicit Copy action. Validate single local file drops and keep notifications free of payload
  text. See [desktop privacy](desktop.md#clipboard-privacy).
- Validate bounded UTF-8 before sending and again after receive integrity checks.
  Keep text in bounded memory, expose it only after acknowledgement, and never
  infer its type from a filename. Do not put payloads in logs or notifications.
  CLI text stdout is the explicit content output channel. See [content](content.md).
- Connectivity probes require the same pinned TLS authentication and safe route
  fallback as transfers. They must not grant trust, write content, or auto-open it.
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
- Keep identity, trust, and unverified incoming files in app-private storage with backup
  disabled. After verification, save files to shared Downloads using a new MediaStore
  row with `IS_PENDING` until the bounded copy closes successfully. Never overwrite
  existing downloads; remove incomplete rows on failure and keep the verified staging
  copy for an explicit retry of Downloads publication. Remove staging after successful publication.
  Received text stays in bounded memory. Public certificate export must never expose
  the private identity file.
- Start reception when the user opens the visible app, after pairing and required
  network permission; keep share intents limited to content review. Show a foreground
  notification with a Stop action, and honor Stop across activity recreation.
  Do not restart after process death or boot. Stop active workers before
  editing/revoking trust. Never open received files/URLs or access the clipboard automatically. Android
  clipboard writes require an explicit Copy action; clipboard reads are forbidden.
- The transfer service is unexported. Share-target input cannot invoke native
  commands or start transfers. Catch exceptions at every JNI entry point, bound
  argument/event arrays, and keep native worker ownership behind the value bridge.
