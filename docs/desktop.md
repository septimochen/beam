# Desktop app

Beam has an optional native SwiftUI/AppKit app on macOS and Qt 6 Widgets tray
app on Linux. Both frontends use the same shared C++ desktop service and
identity store, paired certificates, wire protocol v4, safe direct/Tailscale
fallback, streamed file transfer, and text validation as the CLI. Each frontend owns
only the window, menu bar/tray, clipboard access, and notifications; the core remains
independent of the desktop toolkit.

The app uses Beam's shared three-ray icon. The macOS Dock icon, monochrome
menu-bar template, Linux window/tray resource, and installed launcher all use the
same identity. Every macOS desktop build compiles the layered native appearances
with Xcode's asset compiler; full Xcode is required.
See [icon sources and regeneration](../assets/icons/README.md).

## Build and run

On macOS, install and select full Xcode with Swift 6, then install Ninja
(`brew install ninja`) alongside the normal Beam dependencies. See
[Xcode setup](../assets/icons/README.md#native-macos-appearances). The app uses
system SwiftUI, AppKit, and UserNotifications frameworks; no Qt runtime is needed. Desktop
configuration sets the deployment target to the selected macOS SDK version
(`xcrun --sdk macosx --show-sdk-version`, currently 27.0 on the development Mac),
including existing build trees that cached the old 13.0 target. Use a matching
current OS and dependencies to run that build. `CMAKE_OSX_SYSROOT` selects an
alternative SDK; the desktop deployment target follows it. On Linux, install Qt 6.4+ Widgets
(`qt6-base-dev` and `qt6-qpa-plugins` on Debian/Ubuntu). Ordinary CLI builds do not
find or link either UI toolkit. Normal configuration never downloads dependencies.

```sh
make deps
make desktop
# Linux:
./build-desktop/beam-desktop
# macOS:
open ./build/macos/dev/apps.noindex/beam-desktop.app
# Or run the macOS bundle executable with options:
./build/macos/dev/apps.noindex/beam-desktop.app/Contents/MacOS/beam-desktop --output ./received
```

For a custom Linux Qt prefix, pass `CMAKE_ARGS='-DCMAKE_PREFIX_PATH=/path/to/qt'`.
`make desktop` uses `BUILD_DIR/macos/dev` on macOS with Ninja and
`BUILD_DIR-desktop` on Linux. Override `DESKTOP_BUILD_DIR` to choose another
directory. Existing macOS Qt build directories remain separate. Manual native
configuration requires a fresh directory with `-G Ninja` (or the Xcode generator)
and `-DBEAM_ENABLE_DESKTOP=ON`. The app requires the
macOS/Linux QUIC core; discovery and Tailscale CLI discovery remain optional.
On macOS the application bundle is always generated inside `apps.noindex`,
including with a custom build directory, so Spotlight does not index a second
launchable copy. `make install` publishes the searchable `Beam.app`.

## Install for your user

After installing the build prerequisites and running `make deps`, run:

```sh
make install
```

This builds the CLI and desktop app in a separate Release `build/macos/release`
directory on macOS (`build-install` on Linux). See the
[macOS lifecycle guide](macos-lifecycle.md) for development identities, presets,
validated upgrades, optional data/notification cleanup, and stale registrations.
It installs without `sudo`:

| Platform | CLI | Desktop |
| --- | --- | --- |
| Linux | `~/.local/bin/beam` | `~/.local/bin/beam-desktop`, launcher at `~/.local/share/applications/beam.desktop` |
| macOS | `~/.local/bin/beam` | `~/Applications/Beam.app` |

Add `~/.local/bin` to your shell's `PATH` if needed. On Linux, the launcher uses
`$XDG_DATA_HOME/applications` when `XDG_DATA_HOME` is set. Qt, OpenSSL, Avahi,
and MsQuic remain system package dependencies. Linux installations use the system
MsQuic runtime, so package upgrades apply without reinstalling Beam. For a custom
runtime outside the system loader paths, explicitly configure
`-DBEAM_BUNDLE_MSQUIC=ON` to copy MsQuic into `~/.local/lib/beam`.
See [dependency setup](development.md#build-setup). On macOS, the CLI entry is a
symlink to its private runtime
under `~/.local/lib/beam`, and both it and the app include their non-system dylibs
with local ad-hoc signatures. These are local builds, not notarized distribution
packages. Installed programs do not depend on the build directory.

Override `INSTALL_PREFIX`, `APPLICATIONS_DIR`, `DESKTOP_ENTRY_DIR`, or
`INSTALL_BUILD_DIR` for custom locations. For example:

```sh
make install INSTALL_PREFIX="$HOME/Tools/beam" APPLICATIONS_DIR="$HOME/Applications"
```

Running the command again updates the installation. It does not start Beam.
For a prebuilt CMake tree, `cmake --install BUILD --prefix PREFIX` installs the
CLI and, when enabled, the desktop app; the production macOS bundle goes in
`PREFIX/Beam.app` (development uses `PREFIX/Beam Dev.app`).
Components `cli` and `desktop` allow separate prefixes. The default Linux
desktop entry follows `PREFIX/share/applications`; set `BEAM_DESKTOP_ENTRY_DIR`
at configuration to override it.

Remove a Make installation with:

```sh
make uninstall
```

This removes the CLI, private runtime dependencies, and macOS `Beam.app` or Linux
desktop executable, launcher, and icons. Identities, paired devices, configuration,
app preferences, and received files are retained. Shared `bin`, `lib`, and `share`
directories stay in place. No build directory or compiler is required; CMake and
Make are sufficient, and running it again is safe.

For a custom installation, repeat the same `INSTALL_PREFIX`, `APPLICATIONS_DIR`
(macOS), and `DESKTOP_ENTRY_DIR` (Linux) overrides used for `make install`.
For example:

```sh
make uninstall INSTALL_PREFIX="$HOME/Tools/beam" APPLICATIONS_DIR="$HOME/Applications"
```

Linux's default launcher path follows `XDG_DATA_HOME` just as installation does.
`DESTDIR` staging is also respected. The target uses the standard Make install layout.

## Set up paired devices

Choose **Add device** for QR display, nearby PIN entry, or invitation paste.
Review and approve on both devices; see [pairing](pairing.md). Manual CLI PEM
pairing remains available as an advanced option. Set a peer's numeric address
with `beam peer NAME --endpoint IP:PORT` or its optional Tailscale route. Click
**Refresh devices** after CLI changes. Discovery never chooses or approves a peer.
The app reports missing identity, peers, or addresses with setup guidance.

Select a device and click **Unpair device…** to revoke its local trust. Confirmation
names the device and explains that active transfers and connectivity checks stop.
The picker refreshes and reception restarts for remaining devices; with no peers,
reception stops. Cancel makes no changes. Identities and received files are kept;
the other device's trust record is managed there separately.

Options are `--state-dir DIR`, `--output DIR`, `--listen PORT` (default 4269), and
`--no-discovery`. Reception starts automatically once a paired identity is
available. The default receive folder is `~/Downloads`. `--output` overrides
configuration and the remembered folder for that launch. Use `--help` for usage.

## Everyday sharing

Choose a paired device. Click **Choose file** or drop one local regular file
into the window, then click **Send**. A drop selects the file; it does not send it.
Directories, symlinks, remote URLs, network file URLs, multiple-file drops, and
unsupported filenames are rejected. Both frontends negotiate copy-only drops and
route file drops over the text editor through the same validation. There is one
active sender; **Cancel send**
requests core cancellation, including during blocked network waits.

Reception stays enabled while the app runs, with no Start/Stop receiving buttons
in the window or tray menu. With no configured or remembered folder, files go to
`~/Downloads`, which the app creates if missing. **Choose folder…** selects an
existing alternative and remembers it in native app preferences. Changing folders
restarts reception automatically. If identity/pairing is missing, choose **Add device**. Missing or unsafe custom folders and startup
failures remain visible; choose a valid folder or click **Refresh devices** to retry.
This is app-launch behavior, not an OS-login startup service.
macOS may ask for Downloads access when the receiver first opens that folder;
see [Apple's folder access rules](https://developer.apple.com/documentation/bundleresources/information-property-list/nsdownloadsfolderusagedescription).

Only currently paired devices are accepted. The app receives one item at a time
and starts a fresh listener after success or a peer failure. Idle timeouts restart
quietly.
The trust snapshot is reloaded for each listener; **Refresh devices** restarts
reception to apply pairing changes immediately. Changing folders or refreshing
devices cancels an active incoming transfer before restarting. Empty trust, unsafe
output, and startup failures stop the receiver. LAN advertisement exists only
while its bound listener exists. Advertisement failures leave direct transfers available.

Linux runs one desktop instance per user desktop session. Launching Beam again
restores and focuses the existing window, including when hidden in the tray or
minimized, without creating another tray icon or receiver. Options on later launches
do not reconfigure the running app; quit first to change launch options. This uses
Qt D-Bus (part of Qt 6 base) and requires the desktop session bus. If activation
fails, the new process reports an error instead of opening a duplicate.

Closing the macOS window hides it; use **Open Beam** from the native menu bar or
the Dock to return. Linux hides to the tray when available, otherwise closing
quits. **Quit Beam** cancels and joins workers on both platforms. The app stays
visible at startup. Transfer results remain in the window. macOS notifications
default to off; enable **Transfer notifications** in the application menu to
request OS authorization. Startup and transfer results never request permission.
Linux requests Qt tray notifications where supported.
Notification availability depends on the desktop and OS settings;
see [Qt's tray documentation](https://doc.qt.io/qt-6/qsystemtrayicon.html).
Completion notifications follow size/SHA-256 validation and acknowledgement,
never payload-byte completion. There is no OS-login startup, background daemon,
transfer history, folder synchronization, or automatic retry of a failed send.

## Progress, connectivity, and settings

The app window and tray/menu-bar popup show send/receive progress bars, payload
percentage, and average payload MiB/s. Hashing, connecting, and verification remain
separate stages; 100% payload bytes does not report verified completion.

Select a paired device and click **Check connectivity**. A bounded authenticated
PING/PONG checks its Beam receiver using the saved routes and exact certificate
pinning. It creates no payload/file or receive notification, and reception continues.
Unavailable, timeout, routing, and identity failures remain visible without changing
trust. Checks are asynchronous and cancelled/joined on Quit.

See [configuration](configuration.md) for `~/.config/beam/config`. CLI overrides
win over config; config wins over the remembered folder. No config file is written
by the app. Beam 0.9 requires protocol v4 on both peers; pairing records are retained.

## Clipboard privacy

Clipboard access is explicit. Click **Load clipboard text** in the window or
tray menu to capture the system clipboard once. Review the snapshot and selected
device, then click **Send**. URLs use the same Text selection and 64 KiB UTF-8
limit as other text. An unchanged clipboard snapshot preserves its original line endings;
editing uses the text editor's line endings.

The app does not watch clipboard changes, read it at startup, send automatically,
or write received data to the clipboard automatically. On Linux/X11, the stock Qt
text editor still uses PRIMARY for selection and middle-click paste; explicit
Load/Copy actions use the regular clipboard. A later clipboard change does not
change the loaded snapshot. Only text is supported;
use file selection for images and other binary content. Received text stays in
memory in a selectable text area. **Copy to clipboard** buttons at the top right
of outgoing/received text areas write only when clicked. No text files or history
are created. Nothing is executed or opened. Notifications and status show metadata, never payload text.
Clipboard APIs remain on the GUI thread: macOS uses NSPasteboard and Linux uses
QClipboard. Native menus include standard text editing shortcuts. The shared
service handles all network work; neither frontend has a separate transfer engine.

## Verification

`make desktop-test` builds the app and runs shared worker/controller integration
plus platform UI tests, including automatic reception, first-run folder setup,
remembered folders, startup failures, and worker shutdown. The controller tests
exercise authenticated file/text
transfers, repeated receiving, idle timeout, rejection recovery, unchanged output,
changed trust, cancellation, and worker shutdown. UI tests check local drop
validation, explicit clipboard snapshots, size limits, plain-text status, and
unpaired sends. Native macOS tests additionally exercise error translation,
CLI option validation, paired file/text transfers through the bridge,
notification metadata, repeated receiving, and joined shutdown. Linux tests use
Qt's offscreen platform. Single-instance tests use `dbus-run-session` (the `dbus`
package) to isolate repeated/concurrent launches, hidden/minimized window restoration,
and restart after exit or process termination. They require no tray, discovery
daemon, or public network;
macOS clipboard tests need access to a desktop pasteboard session.

Run complete macOS checks with `make check BUILD_DIR=build/macos/dev
CMAKE_ARGS='-G Ninja -DBEAM_ENABLE_DESKTOP=ON'`; use `BUILD_DIR=build-desktop` and
omit `-G Ninja` for Linux. Linux main-branch CI enables the desktop
build and runs its tests and static analysis; manual macOS/Linux builds do too.
Windows remains the CLI scaffold. Automated tests cannot confirm native menu bar/tray,
drag gestures, clipboard permissions, or OS notification delivery. To check these
on a desktop, pair two devices, send a file by dropping it, send a reviewed
clipboard snapshot, receive repeated items, cancel a transfer, hide/reopen from
the tray, relaunch Beam while its window is hidden or minimized and confirm it
receives focus without adding a tray icon, and quit during an active receive.

`make desktop-live-test` additionally tests the desktop worker's own live LAN
advertisement/withdrawal during repeated paired transfers. It requires Bonjour
or a running Avahi daemon and local multicast access, and also runs in Linux CI.

## Device management

macOS has separate **Share** and **Devices** views; Linux opens **Manage devices**.
The Devices view provides searchable cards, direct/Tailscale routes, individual
connection checks, QR/PIN/invitation pairing, local rename/address edits,
certificate inspection/replacement, and confirmed unpairing. **Backup & restore**
exports public device records and previews imports before adding trust. Inline
errors keep forms open for correction; saving cancels workers and reloads trust.
See [device management and recovery](pairing.md#device-management) for name rules,
verified certificate replacement, conflict handling, and identity recovery limits.
