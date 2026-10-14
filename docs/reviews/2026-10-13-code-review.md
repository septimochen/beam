# Code review — 2026-10-13

Reviewed revision: `25cba43a7e65b3c2f110628a3361f2a6be864f97` (Phase 7).
Scope: current CLI, core transfer/protocol/storage, identity/TLS, discovery/routing,
desktop implementation, and associated tests. This report records findings;
implementation fixes are pending.

## Findings

### 1. P1 — Duplicate stream rejection deadlocks the connection worker

Location: [`src/transport/msquic.cpp:470–479`](../../src/transport/msquic.cpp#L470).

When an authenticated peer opens another stream of a direction already seen,
the callback leaves its local `unique_ptr<QuicStream>` to be destroyed before
returning. The destructor calls `close()`, which requests `ABORT | IMMEDIATE`
shutdown and waits without a deadline for `stopped` (lines 251–269).
MsQuic v2.6.2 queues this shutdown; `IMMEDIATE` does not execute it inline.
The connection worker is therefore waiting for a shutdown callback that it must
itself deliver after the current callback returns. The callback also holds the
connection mutex, preventing normal application cleanup from proceeding.

The configured limit of one stream per direction does not make this unreachable:
MsQuic restores stream credit after a previous stream shuts down. For example,
a second payload stream can arrive while the receiver awaits the final control
acknowledgement. Timeout, cancellation, and desktop worker joining cannot recover
from the blocked worker.

Keep rejected streams owned until callback processing can finish, and perform
their blocking teardown on the application thread. Do not block inside the
connection callback. Add a regression covering another stream after the first
has closed, with bounded receiver teardown.

Evidence: source tracing through Beam's callback/destructor and the locally
pinned MsQuic `MsQuicStreamShutdown`, `QuicStreamTryCompleteShutdown`, and
`QuicStreamSetReleaseStream`. This finding was not reproduced over the network.
See the [upstream shutdown contract](https://github.com/microsoft/msquic/blob/v2.6.2/docs/api/StreamShutdown.md).

### 2. P1 — Acknowledging move drops can delete the original file

Location: [`src/desktop/qt/window.cpp:384–389`](../../src/desktop/qt/window.cpp#L384).

The drop handler stores only the original path, then calls
`acceptProposedAction()`. It also accepts `Qt::MoveAction`, although no file bytes
have been copied or sent. A drag source that deletes its original after a
successful move can consequently remove the user's only copy and leave Beam
with a missing source. This consequence follows from
[Qt's documented move contract](https://doc.qt.io/qt-6/dnd.html#drag-and-drop-actions).

Negotiate `CopyAction` when supported and reject move-only drops. Cover both
move-only and copy-plus-move events, including modifier-selected moves.

Evidence: an offscreen drag-enter/drop sequence with one regular local file
and `Qt::MoveAction` was accepted with `dropAction == 2`; Beam retained only
the source path. Original deletion by a native drag source was not exercised.
The existing UI test covers only `CopyAction`.

### 3. P2 — The text editor bypasses file-drop validation

Location: [`src/desktop/qt/window.cpp:125–131`](../../src/desktop/qt/window.cpp#L125).

The stock `QPlainTextEdit` handles drops on its viewport before the parent window
can apply `dropped_file()`. In Text mode, dropping one regular file onto the
editor inserts its `file://` URL, leaves the selected file empty, and keeps
the content kind as Text. A subsequent Send transfers the path string rather
than the file. Multiple remote URLs also bypass the single-local-file policy.

Disable the editor's default drop handling or route its events through the same
validation and file-selection path as window drops. Test actual viewport event
dispatch in both Text and URL modes.

Evidence: reproduced with offscreen `QDragEnterEvent` and `QDropEvent` dispatch
to the visible editor viewport. Both a local-file URL and multiple HTTPS URLs
were accepted. Existing tests dispatch valid drops only to the parent window.

### 4. P2 — The editor implicitly uses X11 PRIMARY selection

Location: [`src/desktop/qt/window.cpp:125–131`](../../src/desktop/qt/window.cpp#L125).

On Linux/X11, the default Qt text editor publishes selected text to PRIMARY and
reads PRIMARY on middle-click. Beam does not override those interactions.
Selecting part of a reviewed clipboard snapshot therefore writes to the system
selection, and middle-click can import text without the Load action. This
contradicts the explicit no-PRIMARY behavior in [`docs/desktop.md`](../desktop.md#clipboard-privacy).

Prevent implicit PRIMARY reads and selection publication while retaining the
intended editing interactions. Verify the privacy contract with an X11-backed
UI test; macOS/offscreen tests do not exercise this platform behavior.

Evidence: source-confirmed in
[Qt 6.11.2's text control](https://github.com/qt/qtbase/blob/v6.11.2/src/widgets/widgets/qwidgettextcontrol.cpp#L598-L605),
with middle-click reads at lines 1678–1689. Linux/X11 runtime validation was
unavailable in this review.

### 5. P2 — IPv4-mapped IPv6 routes fail and are not deduplicated

Location: [`src/core/routing.cpp:38–41`](../../src/core/routing.cpp#L38).

`numeric_endpoint()` retains an IPv4-mapped IPv6 address as IPv6. The transport
then passes `AF_INET6` to MsQuic `ConnectionStart`. With the current macOS build,
a paired transfer to `[::ffff:127.0.0.1]:49531` times out, while equivalent IPv4
and ordinary IPv6 loopback transfers succeed. This also affects the mapped
Tailscale address form expressly accepted by [`docs/tailscale.md`](../tailscale.md).

Equivalent mapped and IPv4 saved routes also survive string-based deduplication:
`100.64.0.20:4269` and `[::ffff:100.64.0.20]:4269` produce two attempts.
Canonicalize mapped addresses to their IPv4 form before selection and transport.
Add mapped-versus-IPv4 deduplication and authenticated transfer coverage.

Evidence: fresh reciprocal identities and the same localhost receiver produced
sender exit 1 / connection timeout for the mapped form, but verified four text
bytes with exit 0 for `127.0.0.1` and `::1`. A separate pure route-selection
fixture confirmed the two equivalent routes were both returned.

## Validation

- `make check BUILD_DIR=build-desktop BUILD_TYPE=Release`: formatting and the
  complete desktop-enabled build passed. Eleven of sixteen tests passed inside
  the sandbox; five integration tests failed because localhost sockets were
  restricted, before exercising transfers.
- `ctest --test-dir build-desktop --build-config Release --output-on-failure --rerun-failed`
  with localhost socket access: all five failed tests passed. Together these
  runs passed all sixteen existing tests; no implementation changes occurred
  between them.
- Temporary offscreen drop fixtures and paired loopback route fixtures verified
  the additional cases described above. They used disposable files and identities.
- Linux/X11 native clipboard behavior and native drag-source deletion were
  assessed from Qt's primary documentation/source, not runtime-tested.
- Linux builds, live DNS-SD, and a new static-analysis run were not performed.

The passing suite does not cover these findings. No other substantiated defects
were identified in the reviewed protocol validation, verified publication,
exact-leaf identity authorization, or discovery subprocess cleanup paths.
