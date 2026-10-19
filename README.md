# Beam

Send a file, text, or URL directly between macOS, Linux, and Android devices.
Beam discovers receivers on your LAN, encrypts transfers with QUIC, and verifies
each payload before reporting success. No account or cloud service is required.

Beam transfers one payload at a time between explicitly paired devices, with an
macOS/Linux CLI and desktop apps and an Android Kotlin/JNI app.
See the [roadmap](docs/ROADMAP.md) for upcoming features.

## Quick start

### 1. Build Beam on both devices

You'll need CMake 3.25+, a C++23 compiler, Git, Perl, Make, and OpenSSL development
libraries. LAN discovery uses Bonjour on macOS or Avahi on Linux.
See [build setup](docs/development.md#build-setup) for platform dependencies.

```sh
git clone https://github.com/septimochen/beam.git
cd beam
make deps   # provisions pinned MsQuic and pairing dependencies
make build
./build/beam --help
```

The examples below run from the repository directory on each device.

### 2. Pair the devices

On the computer or receiver:

```sh
./build/beam pairing host --name laptop
```

On the other device:

```sh
./build/beam pairing join --name desktop
# Paste the invitation printed by the host, then approve on both devices.
```

Identity creation, public certificate exchange, and both transfer addresses are
handled automatically. Both devices must be on the same LAN. For nearby PIN
pairing, run `beam pairing nearby`, then `beam pairing join --endpoint IP:PORT`
and enter the PIN shown on the other screen. Secrets are read from stdin.

Desktop apps provide **Add device**, QR display, nearby selection, and invitation
paste. Android also scans QR codes. No accounts or internet service are required.
See [pairing](docs/pairing.md) for recovery and advanced manual PEM pairing.

To revoke a device, select it and choose **Unpair device** in the app, or run
`./build/beam unpair NAME` (use `./build/beam peers` to list names). This removes
trust on this device only; pair again to reconnect.

### 3. Start the receiver

```sh
mkdir -p received
./build/beam receive --output ./received --name Laptop
```

Allow UDP port 4269 through its firewall. The receiver advertises itself on the
LAN, accepts one file or text (including URLs) from a paired device, and exits. Run it again
for another transfer.

### 4. Send a file

On the sender:

```sh
./build/beam send hello.txt laptop
```

The verified file appears in the receiver's `received` directory. If you need to
find or update its address:

```sh
./build/beam devices
./build/beam peer laptop --endpoint 192.168.1.60:4269
```

Use an endpoint from discovery. A changed address does not require re-pairing.

### 5. Send text or a URL

Restart the receiver for each transfer, then send to its paired name:

```sh
./build/beam text "hello" laptop
./build/beam text "https://example.com/page" laptop
```

Text, including URLs, uses up to 64 KiB of UTF-8. Apps display it in memory with
explicit **Copy to clipboard** buttons; the CLI writes received text verbatim to
stdout. No text files are created automatically. See [content transfers](docs/content.md).
Both devices must use `beam/4` (Beam 0.9+); update older peers. Existing pairing
records continue to work. Check a paired receiver with `./build/beam check laptop`.

Desktop/CLI defaults can be set in `~/.config/beam/config`; see
[configuration](docs/configuration.md) for the receive folder, port, discovery,
and timeout. Android saves received files to shared Downloads and has port/timeout **Settings**.

## Desktop sharing

Run `make install` to build Release binaries and install the CLI in `~/.local/bin`
and the desktop app. Linux gets an application-menu entry; macOS gets
`~/Applications/Beam.app`. Add `~/.local/bin` to your `PATH` if needed. No `sudo`
is required. See [installation options](docs/desktop.md#install-for-your-user).
Use `make uninstall` to remove the installed programs and runtime while retaining
pairing, settings, and received files.
On macOS, build bundles stay in `apps.noindex`; install/uninstall also clean
obsolete Beam bundles from this checkout's old CMake build directories, so they
do not remain as extra application search results.

macOS uses a native SwiftUI/AppKit app; install and select full Xcode and install
Ninja (`brew install ninja`). Desktop builds target the selected macOS SDK
version and always compile native icon appearances. See [Xcode setup](assets/icons/README.md#native-macos-appearances).
Linux uses Qt 6 Widgets (`qt6-base-dev` and
`qt6-qpa-plugins` on Debian/Ubuntu). Run `make desktop`, then launch
`build-desktop/beam-desktop` on Linux or `open build/macos/dev/apps.noindex/beam-desktop.app`
on macOS. Choose **Add device** to pair, then choose a device, drop or select one
file, then click **Send**. After pairing, reception starts automatically when the
app opens. Files go to `~/Downloads` unless you configure or choose another folder;
the app remembers folder selections.

**Load clipboard text** captures a snapshot for review; sharing requires a
separate **Send** click. Enable **Transfer notifications** in the macOS application
menu to receive OS notifications. See [macOS install and cleanup](docs/macos-lifecycle.md)
for development builds and stale registrations. Tray notifications report verified transfers, and
**Quit Beam** stops workers. See [desktop setup and privacy](docs/desktop.md).

## Android sharing

Build an ARM64 APK with `make android` after the [Android build setup](docs/android.md#build).
`make android-setup` installs the latest stable SDK/NDK/CMake tools;
`make android-toolchain` shows the installed versions selected for builds.
With USB debugging enabled and your phone connected/authorized, run
`make android-install`; see [phone installation](docs/android.md#install-on-your-phone).
Open **Manage devices → Add device** on Android and **Add device** on the other
device. Scan its QR code or enter its nearby PIN, then approve on both screens.
Choose a paired device and one file or text (including URLs), then tap **Send**. Other apps' **Share → Beam** opens the
same review screen. Opening Beam starts reception after pairing and required
permissions; **Stop receiving** pauses it.
Received files save automatically to **Downloads**. The Receive area shows progress
and a brief completion message, with a separate notification when enabled. Use
**Downloads** to find files. **Retry saving** appears only if a Downloads save failed
or an older private file still needs saving.

See [Android setup, pairing, and tests](docs/android.md) for QR/PIN pairing,
foreground controls, APK paths, and platform verification.

## Useful options and tips

For devices on an existing tailnet, discover an address and save it alongside the
paired device's LAN endpoint:

```sh
./build/beam devices --tailscale
./build/beam peer laptop --tailscale-endpoint 100.64.0.20:4269
./build/beam send hello.txt laptop --route tailscale
```

The default route tries the saved direct address before a Tailscale address, with
fallback only for connection failures before authentication. `--route direct` or
`--route tailscale` selects one class; an explicit `--endpoint` selects exactly
one address. See [Tailscale setup](docs/tailscale.md) for trust, build dependencies,
and peer-record compatibility.

- Use `beam peers` to list trusted devices and `beam unpair NAME` to remove one.
  Restart a running receiver after removal to apply the change.
- Add `--endpoint IP:PORT` to `send`, `text`, or `check` to use an address for just that transfer.
  Discovery supplies addresses; pairing verifies identity.
- Use `[IPv6]:4269` for an IPv6 endpoint.
- Add `--no-discovery` to `receive` to keep it unadvertised; omit `--name` in that case.
- Add `--timeout 120` to `send` or `receive` for longer individual waits
  (the default is 30 seconds). `devices --timeout 5` browses for five seconds.
- Press **Ctrl-C** to cancel. Add `--no-progress` to `send` or `receive` for scripts;
  progress goes to stderr and success to stdout.
- Beam refuses to overwrite existing files. If a transfer fails at the final
  acknowledgement, check the destination before retrying: a verified file may
  already be there. Keep the source file unchanged during transfer.
- Use a plain, printable ASCII filename; hidden names, reserved names, and
  symlinks are rejected.

If discovery finds nothing, check that the receiver is running on the same LAN
and Avahi is running on Linux. See [discovery troubleshooting](docs/discovery.md#verification-and-troubleshooting)
for more help. Existing `send IP:PORT FILE --cert ... --key ... --ca ...`
commands are still supported; see [dedicated-CA setup](docs/tls.md#dedicated-ca-transfers).

## Contributing

Start with the [roadmap](docs/ROADMAP.md) and [development guide](docs/development.md).
Keep changes focused and include tests for behavior changes.

```sh
make format          # apply clang-format 23
make check           # check formatting, clang-tidy/static analysis, build, and tests
make lint            # run clang-tidy/static analysis for the configured build
make discovery-test  # test live LAN discovery; requires Bonjour/Avahi
make content-test    # test text validation, in-memory reception, and paired probes/transfers
make routing-test    # test Tailscale fixtures and safe endpoint fallback
make desktop-test    # build desktop app, test worker transfers and UI
make android-check   # Android formatting, lint, unit tests, and debug APK
```

See [build setup](docs/development.md#build-setup) for formatter and lint tool setup,
and [build options](docs/development.md#build-options) for Release and other configurations.
For deeper implementation details, read [architecture](docs/architecture.md),
[protocol](docs/protocol.md), and [security](docs/security.md).

Manage paired devices with `beam peer NAME --rename NEW_NAME --endpoint IP:PORT`.
Back up with `beam peers export devices.beam-backup`; preview recovery with
`beam peers import devices.beam-backup`, then add `--approve` to restore.
Native apps include a Devices management screen. See [device management](docs/pairing.md#device-management)
for certificate replacement, public-only backups, and identity recovery limits.
