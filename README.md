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
make deps   # downloads and builds MsQuic; takes a few minutes
make build
./build/beam --help
```

The examples below run from the repository directory on each device.

### 2. Create an identity and pair the devices

On each device, create an identity and export its public certificate:

```sh
./build/beam identity init
./build/beam identity export > my-device.pem
```

Name the exported files `laptop.pem` on the receiver and `desktop.pem` on the
sender, then exchange the public certificates. Compare the full fingerprint shown by
`identity init` with the fingerprint on the original device, in person or through
an independently trusted channel. Keep private keys on their original device.

For a receiver called `laptop` and a sender called `desktop`, run on the sender:

```sh
./build/beam pair laptop --cert laptop.pem --fingerprint VERIFIED_LAPTOP_SHA256 \
  --endpoint 192.168.1.50:4269
```

On the receiver:

```sh
./build/beam pair desktop --cert desktop.pem --fingerprint VERIFIED_DESKTOP_SHA256
```

Use the exchanged certificate paths and replace the placeholders with each
verified 64-character fingerprint. See [pairing setup](docs/pairing.md) for details.

### 3. Start the receiver

```sh
mkdir -p received
./build/beam receive --output ./received --name Laptop
```

Allow UDP port 4269 through its firewall. The receiver advertises itself on the
LAN, accepts one file, text, or URL from a paired device, and exits. Run it again
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
./build/beam url "https://example.com/page" laptop
```

Text (up to 64 KiB of UTF-8) and HTTP(S) URLs (up to 8 KiB) are saved as
`text-TRANSFER_ID.txt` or `url-TRANSFER_ID.txt` in `received`. Beam never opens
links, executes text, or copies content to the clipboard. See [content transfers](docs/content.md)
for validation rules and options. Both devices must use this release's `beam/3`
protocol; existing pairing records continue to work.

## Desktop sharing

macOS 13+ uses a native SwiftUI/AppKit app; install Xcode Command Line Tools and
Ninja (`brew install ninja`). Linux uses Qt 6 Widgets (`qt6-base-dev` and
`qt6-qpa-plugins` on Debian/Ubuntu). Run `make desktop`, then launch
`build-desktop/beam-desktop` on Linux or `open build-desktop-native/beam-desktop.app`
on macOS. Use the same CLI pairing setup, choose a device, drop or select one
file, then click **Send**. Choose a folder and click **Start receiving** to
receive repeated transfers.

**Load clipboard text** captures a snapshot for review; sharing requires a
separate **Send** click. Tray notifications report verified transfers, and
**Quit Beam** stops workers. See [desktop setup and privacy](docs/desktop.md).

## Android sharing

Build an APK with `make android` after the [Android build setup](docs/android.md#build).
Install it, open **Manage devices and identity**, exchange public certificates,
verify full fingerprints, and pair on both devices. Choose a paired device and
one file, text, or URL, then tap **Send**. Other apps' **Share → Beam** opens the
same review screen. Tap **Start receiving** to accept verified transfers; use
**Save a received file…** to export from private storage.

See [Android setup, pairing, and tests](docs/android.md) for numeric addresses,
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
- Add `--endpoint IP:PORT` to `send`, `text`, or `url` to use an address for just that transfer.
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
make content-test    # test typed text/URL validation and paired transfers
make routing-test    # test Tailscale fixtures and safe endpoint fallback
make desktop-test    # build desktop app, test worker transfers and UI
make android-check   # Android formatting, lint, unit tests, and debug APK
```

See [build setup](docs/development.md#build-setup) for formatter and lint tool setup,
and [build options](docs/development.md#build-options) for Release and other configurations.
For deeper implementation details, read [architecture](docs/architecture.md),
[protocol](docs/protocol.md), and [security](docs/security.md).
