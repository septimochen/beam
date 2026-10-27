# Text transfers

Beam 0.9 uses protocol v4 (`beam/4`) for files and UTF-8 text. URLs are ordinary
text: use `beam text "https://example.com/page" laptop`. The separate `beam url`
command, URL content kind, and `send_url` API have been retired. Update both peers;
v3 peers fail ALPN negotiation. Existing identities and paired certificates remain
usable without re-pairing.

Initialize and reciprocally pair devices as described in [pairing](pairing.md).
Start a receiver, then send text to its paired name:

```sh
beam receive --output ./received
beam text "hello 🌍" laptop
beam text "https://example.com/page" laptop
```

Text is `text/plain`, at most 65,536 UTF-8 bytes. Empty text, tabs, LF and CR are
allowed; bytes and line endings are preserved without adding a newline. Invalid
UTF-8, NUL, other C0 controls, DEL, and C1 controls are rejected. URL-looking text
uses these same rules; Beam never fetches or opens it or interprets its scheme.

Text is received in bounded memory and returned as `TransferResult.text` after
size, SHA-256, UTF-8 validation, and acknowledgement. It creates no temporary or
final text files. Apps show the most recently received text in a selectable text
area with a **Copy to clipboard** button at the top right. The outgoing text area
has its own explicit Copy button. New text replaces the previous in-memory view;
there is no persistent history or automatic clipboard write.

The CLI writes `Received text: `, the received bytes, and a terminating newline
to stdout for each text transfer. Existing line breaks in the text are preserved.
Listener/progress diagnostics go to stderr. Redirect stdout to save the labeled output.
The CLI defaults to `~/Downloads` for files when no folder is configured.
File receives retain safe, exclusive publication in the selected directory.

`--state-dir`, `--endpoint IP:PORT`, `--route`, `--timeout SECONDS`, and
`--no-progress` work as for paired file sends. Quote shell arguments, and use `--`
after options when content begins with `--`. Arguments may appear in shell
history/process listings; avoid passing secrets this way.

`send_text` snapshots caller content before callbacks. `receive_content` accepts
files, text, and connectivity probes; `receive_file` still rejects text offers.
A probe result has `connectivity_check=true` and no payload. The CLI continues
listening after each transfer or probe until interrupted; `--once` exits after a
file or text transfer. Apps restart their listener quietly. A failed completion
exchange does not display or emit received text. File publication can already
have completed when acknowledgement fails, as before.

Run `make content-test` for UTF-8 boundaries, URL-as-text behavior, malformed input,
integrity, cancellation, authenticated probes, and no text-file publication.
