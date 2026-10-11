# Desktop app

Phase 7 adds an optional Qt 6 Widgets tray app on macOS/Linux. It uses the same
identity store, paired certificates, wire protocol v3, safe direct/Tailscale
fallback, streamed file transfer, and text/URL validation as the CLI. Qt owns
only the window, tray, clipboard access, and notifications; the core remains
independent of the desktop toolkit.

## Build and run

Install Qt 6.4+ Widgets (`brew install qtbase` on macOS, or `qt6-base-dev` and
`qt6-qpa-plugins` on Debian/Ubuntu), alongside the normal Beam dependencies.
Qt provides one cross-platform implementation of tray integration, native file
dialogs, drag-and-drop, clipboard access, and notifications. It is optional:
ordinary CLI builds do not find or link Qt. Normal configuration never downloads it.

```sh
make deps
make desktop
# Linux:
./build-desktop/beam-desktop
# macOS:
open ./build-desktop/beam-desktop.app
# Or run the macOS bundle executable with options:
./build-desktop/beam-desktop.app/Contents/MacOS/beam-desktop --output ./received
```

For a custom Qt prefix, pass `CMAKE_ARGS='-DCMAKE_PREFIX_PATH=/path/to/qt'`.
`make desktop` uses `BUILD_DIR-desktop` (by default `build-desktop`). You can
also configure any build with `-DBEAM_ENABLE_DESKTOP=ON`. The app requires the
macOS/Linux QUIC core; discovery and Tailscale CLI discovery remain optional.
`cmake --install` installs the macOS app bundle or the Linux executable. Runtime
Qt/MsQuic/OpenSSL libraries must remain installed; no standalone installer or
library bundling is provided.

Initialize identity and reciprocally pair devices with the CLI using independently
verified full fingerprints; see [pairing](pairing.md). Set a peer's numeric address
with `beam peer NAME --endpoint IP:PORT` or its optional Tailscale route. Click
**Refresh devices** after CLI changes. Discovery never chooses or approves a peer.
The app reports missing identity, peers, or addresses with setup guidance.

Options are `--state-dir DIR`, `--output DIR`, `--listen PORT` (default 4269), and
`--no-discovery`. They select existing state/output directories and do not
automatically start receiving. Use `--help` for usage.

## Everyday sharing

Choose a paired device. Click **Choose file** or drop one local regular file
into the window, then click **Send**. A drop selects the file; it does not send it.
Directories, symlinks, remote URLs, network file URLs, multiple-file drops, and
unsupported filenames are rejected. There is one active sender; **Cancel send**
requests core cancellation, including during blocked network waits.

Choose an existing receive folder, then click **Start receiving**. Only currently
paired devices are accepted. The app receives one item at a time and starts a
fresh listener after success or a peer failure. Idle timeouts restart quietly.
The trust snapshot is reloaded for each listener; **Stop receiving** and start
again to apply a revocation immediately. Empty trust, unsafe output, and startup
failures stop the receiver. LAN advertisement exists only while its bound listener
exists. Advertisement failures leave direct transfers available.

Closing the window hides it when a system tray is available. Use **Open Beam**
from the tray to return, and **Quit Beam** to cancel and join workers. Without a
tray, closing quits the app. The app stays visible at startup. Transfer results
remain in the window; Qt tray notifications are also requested on systems that
support them. Notification availability depends on the desktop and OS settings;
see [Qt's tray documentation](https://doc.qt.io/qt-6/qsystemtrayicon.html).
Completion notifications follow size/SHA-256 validation and acknowledgement,
never payload-byte completion. There is no automatic startup, background daemon,
transfer history, folder synchronization, or automatic retry of a failed send.

## Clipboard privacy

Clipboard access is explicit. Click **Load clipboard text** in the window or
tray menu to capture the system clipboard once. Review the snapshot and selected
device, then click **Send**. Edit the text if needed, or select **URL** to validate
and send a single HTTP(S) URL. Text and URLs retain the core's 64 KiB/8 KiB UTF-8
byte limits. An unchanged clipboard snapshot preserves its original line endings;
editing uses the text editor's line endings.

The app does not watch clipboard changes, read it at startup, use the X11 primary
selection, send automatically, or write received data to the clipboard. A later
clipboard change does not change the loaded snapshot. Only text is supported;
use file selection for images and other binary content. Received text and URLs
are saved under locally generated names in the receive folder. Nothing is
executed or opened. Notifications and status show metadata, never payload text.
Clipboard APIs remain on the GUI thread; see [Qt's clipboard documentation](https://doc.qt.io/qt-6/qclipboard.html).

## Verification

`make desktop-test` builds the app and runs worker/controller integration and
offscreen UI tests. The controller tests exercise authenticated file/text/URL
transfers, repeated receiving, idle timeout, rejection recovery, unchanged output,
changed trust, cancellation, and worker shutdown. UI tests check local drop
validation, explicit clipboard snapshots, size limits, plain-text status, and
unpaired sends. They require no tray, discovery daemon, or public network.

Run the complete checks with `make check BUILD_DIR=build-desktop
CMAKE_ARGS='-DBEAM_ENABLE_DESKTOP=ON'`. Linux main-branch CI enables the desktop
build and runs its tests and static analysis; manual macOS/Linux builds do too.
Windows remains the CLI scaffold. Offscreen tests cannot confirm native tray,
drag gestures, clipboard permissions, or OS notification delivery. To check these
on a desktop, pair two devices, send a file by dropping it, send a reviewed
clipboard snapshot, receive repeated items, cancel a transfer, hide/reopen from
the tray, and quit during an active receive.

`make desktop-live-test` additionally tests the desktop worker's own live LAN
advertisement/withdrawal during repeated paired transfers. It requires Bonjour
or a running Avahi daemon and local multicast access, and also runs in Linux CI.
