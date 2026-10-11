# Text and URL transfers

Phase 5 adds explicitly typed content over the same authenticated QUIC connection
used for files. Initialize and reciprocally pair both devices as described in
[pairing](pairing.md), then start a receiver for each transfer:

```sh
mkdir -p received
beam receive --output ./received
```

Send UTF-8 text or one HTTP(S) URL to a saved peer alias:

```sh
beam text "hello" macbook
beam text "你好 🌍" macbook
beam url "https://example.com/page?q=hello#section" macbook
```

`--state-dir`, `--endpoint IP:PORT`, `--timeout SECONDS`, and `--no-progress` work
as for paired file sends. These commands require paired credentials. The reusable
core APIs also support the existing dedicated-CA credentials. Shell-quote content
to preserve spaces and avoid shell expansion. Use `--` after all options when
content begins with `--`:

```sh
beam text --timeout 60 -- "--literal text" macbook
```

Text is `text/plain`, up to 65,536 UTF-8 bytes. Empty text, tabs, LF and CR are
allowed. Invalid UTF-8, NUL, other C0 controls, DEL, and C1 controls are rejected.
Bytes and line endings are preserved; no newline is added. The limit counts bytes,
not characters.

URLs are `text/uri-list`, with exactly one absolute HTTP or HTTPS URL, up to
8,192 bytes. Beam accepts ASCII DNS hostnames, IPv4 addresses, bracketed IPv6
addresses, and optional ports from 1 through 65,535. Encode non-ASCII characters
and spaces using percent encoding; use an ASCII/punycode hostname for IDNs. Raw
spaces, controls, backslashes, malformed percent escapes, embedded credentials,
multiple URLs, and other schemes are rejected. This validates supported syntax;
it does not fetch the URL or establish that the target exists or is safe.

The receiver stores text as `text-TRANSFER_ID.txt` and a URL as
`url-TRANSFER_ID.txt` inside the selected directory. The sender does not choose
these names. The content kind is explicit in the offer and returned by the core;
it is independent of the storage extension. Existing files and symlinks are never
overwritten. Size, SHA-256, and content validation must pass before publication;
completion follows the acknowledgement exchange.

Received content is never executed, opened in a browser, copied to the clipboard,
or printed to the terminal. Progress and results show the type, generated filename,
and byte count. Read or open the saved content explicitly. Shell arguments may be
visible in local shell history/process listings, so avoid passing secrets that way.

Failure or cancellation before publication removes partial output. A failure in
the final acknowledgement can leave verified output, as with file transfers. A
retry uses a fresh transfer ID and may create another copy; Beam does not deduplicate
content or maintain history.

Both peers must use protocol v3 (`beam/3`); update Phase 1–4 peers before sending
any kind of content. Stored device identities and pairing records remain usable.
Discovery advertises `v=3` and excludes receivers with incompatible protocol
metadata. See [protocol](protocol.md) for the wire format.

`send_text`/`send_url` snapshot bounded caller content before invoking callbacks.
`receive_content` accepts all three kinds and returns `TransferResult.kind` with
the saved basename and byte count. The existing `receive_file` API stays file-only
and rejects text/URL offers before acceptance. All APIs retain calling-thread
progress, stop-token/predicate cancellation, and typed transfer errors.

Run `make content-test` for validation and authenticated localhost transfers.
These tests are part of `make check` and Linux CI, and need no discovery daemon.
