# Desktop app

Beam has an optional native SwiftUI/AppKit app on macOS and Qt 6 Widgets tray
app on Linux. Both frontends use the same shared C++ desktop service and
identity store, paired certificates, wire protocol v3, safe direct/Tailscale
fallback, streamed file transfer, and text/URL validation as the CLI. Each frontend owns
only the window, menu bar/tray, clipboard access, and notifications; the core remains
independent of the desktop toolkit.

## Build and run

On macOS, install Xcode Command Line Tools with Swift 6 and Ninja (`brew install
ninja`), alongside the normal Beam dependencies. The native UI requires macOS 13+
and uses system SwiftUI, AppKit, and UserNotifications frameworks; no Qt runtime
is needed. The actual minimum OS of installed MsQuic/OpenSSL libraries must also
match the machine running the app. On Linux, install Qt 6.4+ Widgets
(`qt6-base-dev` and `qt6-qpa-plugins` on Debian/Ubuntu). Ordinary CLI builds do not
find or link either UI toolkit. Normal configuration never downloads dependencies.

```sh
make deps
make desktop
# Linux:
./build-desktop/beam-desktop
# macOS:
open ./build-desktop-native/beam-desktop.app
# Or run the macOS bundle executable with options:
./build-desktop-native/beam-desktop.app/Contents/MacOS/beam-desktop --output ./received
```

For a custom Linux Qt prefix, pass `CMAKE_ARGS='-DCMAKE_PREFIX_PATH=/path/to/qt'`.
`make desktop` uses `BUILD_DIR-desktop-native` on macOS with Ninja and
`BUILD_DIR-desktop` on Linux. Override `DESKTOP_BUILD_DIR` to choose another
directory. Existing macOS Qt build directories remain separate. Manual native
configuration requires a fresh directory with `-G Ninja` (or the Xcode generator)
and `-DBEAM_ENABLE_DESKTOP=ON`. The app requires the
macOS/Linux QUIC core; discovery and Tailscale CLI discovery remain optional.
`cmake --install` installs the macOS app bundle or the Linux executable. Runtime
MsQuic/OpenSSL libraries (plus Qt on Linux) must remain installed; no standalone installer or
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
unsupported filenames are rejected. Native macOS drops negotiate copy only and
use the same file selection policy even over the text editor. There is one active sender; **Cancel send**
requests core cancellation, including during blocked network waits.

Choose an existing receive folder, then click **Start receiving**. Only currently
paired devices are accepted. The app receives one item at a time and starts a
fresh listener after success or a peer failure. Idle timeouts restart quietly.
The trust snapshot is reloaded for each listener; **Stop receiving** and start
again to apply a revocation immediately. Empty trust, unsafe output, and startup
failures stop the receiver. LAN advertisement exists only while its bound listener
exists. Advertisement failures leave direct transfers available.

Closing the macOS window hides it; use **Open Beam** from the native menu bar or
the Dock to return. Linux hides to the tray when available, otherwise closing
quits. **Quit Beam** cancels and joins workers on both platforms. The app stays
visible at startup. Transfer results remain in the window. macOS requests OS
notification authorization on the first transfer result; declining it leaves the
in-app status available. Linux requests Qt tray notifications where supported.
Notification availability depends on the desktop and OS settings;
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
Clipboard APIs remain on the GUI thread: macOS uses NSPasteboard and Linux uses
QClipboard. Native menus include standard text editing shortcuts. The shared
service handles all network work; neither frontend has a separate transfer engine.

## Verification

`make desktop-test` builds the app and runs shared worker/controller integration
plus platform UI tests. The controller tests exercise authenticated file/text/URL
transfers, repeated receiving, idle timeout, rejection recovery, unchanged output,
changed trust, cancellation, and worker shutdown. UI tests check local drop
validation, explicit clipboard snapshots, size limits, plain-text status, and
unpaired sends. Native macOS tests additionally exercise error translation,
CLI option validation, paired file/text/URL transfers through the bridge,
notification metadata, repeated receiving, and joined shutdown. Linux tests use
Qt's offscreen platform. They require no tray, discovery daemon, or public network;
macOS clipboard tests need access to a desktop pasteboard session.

Run complete macOS checks with `make check BUILD_DIR=build-desktop-native
CMAKE_ARGS='-G Ninja -DBEAM_ENABLE_DESKTOP=ON'`; use `BUILD_DIR=build-desktop` and
omit `-G Ninja` for Linux. Linux main-branch CI enables the desktop
build and runs its tests and static analysis; manual macOS/Linux builds do too.
Windows remains the CLI scaffold. Automated tests cannot confirm native menu bar/tray,
drag gestures, clipboard permissions, or OS notification delivery. To check these
on a desktop, pair two devices, send a file by dropping it, send a reviewed
clipboard snapshot, receive repeated items, cancel a transfer, hide/reopen from
the tray, and quit during an active receive.

`make desktop-live-test` additionally tests the desktop worker's own live LAN
advertisement/withdrawal during repeated paired transfers. It requires Bonjour
or a running Avahi daemon and local multicast access, and also runs in Linux CI.
