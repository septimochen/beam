# Android

Beam's Android app sends and receives the same authenticated `beam/4` file,
and text transfers as the CLI and desktop apps. It supports Android 10+
(API 29), with ARM64 (`arm64-v8a`) libraries for phones and ARM64 emulators. The app uses Kotlin
and JNI over the existing C++ identity store and joined transfer workers.

The launcher uses Beam's adaptive three-ray icon with day/night colors and a
monochrome layer for themed icons. Transfer notifications use the monochrome
mark without a launcher background. See [shared icon sources](../assets/icons/README.md).

To remove trust, open **Manage devices → More → Unpair device**, then confirm.
Active transfers, file staging, and connectivity checks stop before removal.
Reception resumes for remaining devices if it was enabled; an explicit Stop is
preserved. Removing the last device stops reception. Your identity and received
files remain. The other device must be unpaired separately.

## Build

Install JDK 17+, Android command-line tools (or Android Studio), CMake 3.25+,
Ninja, Git, Perl, and Make. Set `JAVA_HOME` and `ANDROID_HOME` for those installs.
Install the latest stable SDK packages, provision the native libraries, and build:

```sh
make android-setup
make android-deps
make android
```

`android-setup` queries the SDK Manager's stable channel and installs the newest
platform, Build Tools, NDK, CMake, and Platform Tools. Run it again when you want
to upgrade. It requires `sdkmanager` on `PATH` or in
`$ANDROID_HOME/cmdline-tools/latest/bin`; accept SDK licenses when prompted.
`make android-setup ANDROID_SETUP_DRY_RUN=ON` lists the packages without installing.
See the [SDK Manager documentation](https://developer.android.com/tools/sdkmanager).

Builds use the newest complete, stable packages already installed in that SDK,
without querying the network for tool versions. `make android-toolchain` shows
the selection. Make and Gradle share the same selector, including direct Gradle
and Android Studio builds. Preview packages are excluded. Beam currently needs
platform 37+, NDK 28+, and CMake 3.25+; Android 10 remains the minimum device OS.
Compile SDK (including minor API levels) and target SDK follow the selected platform.

Optional environment variables or Make arguments choose specific installed versions:

| Override | Value |
| --- | --- |
| `ANDROID_PLATFORM_VERSION` | Platform suffix, such as `37.0` |
| `ANDROID_BUILD_TOOLS_VERSION` | Build Tools version |
| `ANDROID_NDK_VERSION` | Side-by-side NDK version |
| `ANDROID_CMAKE_VERSION` | SDK CMake version |
| `ANDROID_NDK` | Explicit NDK directory; must match `ANDROID_NDK_VERSION` if both are set |

For example, `make android-deps android ANDROID_NDK_VERSION=YOUR_VERSION` uses
that NDK for both native dependencies and the APK. Use shell exports for the same
selection in Android Studio/direct Gradle. The version overrides also select
packages for `android-setup`. Remove an old `ANDROID_NDK` export to resume automatic
NDK selection. SDK discovery uses `ANDROID_HOME`, `ANDROID_SDK_ROOT`,
`android/local.properties` (`sdk.dir`), then `build/android-tools/sdk`.
Gradle still needs the standard SDK environment or `sdk.dir` configuration.
Changing NDKs uses separate native build caches; rerun `android-deps` after a change.

### VS Code terminals on macOS

The workspace's `.vscode/settings.json` sets `JAVA_HOME`, `ANDROID_HOME`, and
`PATH` for new integrated terminals on Apple Silicon Macs. It uses Homebrew's
JDK 17 at `/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home` and
the SDK at `~/Library/Android/sdk`. SDK tools installed through Android Studio
or Homebrew are included on `PATH`. These locations stay outside disposable
project build directories. Change the paths for an Intel Mac or another install.
The settings configure paths; they do not install a toolchain.

If the tools are not installed yet, install the prerequisites:

```sh
brew install openjdk@17
brew install --cask android-commandlinetools
```

Then close existing VS Code terminals and open a new one so it picks up the
workspace environment. Create the SDK directory and review its licenses:

```sh
mkdir -p "$ANDROID_HOME"
sdkmanager --sdk_root="$ANDROID_HOME" --licenses
java -version
make android-setup
make android-toolchain
make android-deps
make android
```

`ANDROID_SDK_ROOT` is deprecated; `ANDROID_HOME` is sufficient. No NDK/version
override or `GRADLE_USER_HOME` is needed for the default build. These terminal
settings do not configure terminals outside VS Code or Android Studio's Gradle JDK.

The checked-in Gradle wrapper validates its distribution SHA-256; AGP provides
built-in Kotlin. These tested build dependencies retain explicit versions.
`make android-deps` explicitly downloads MsQuic v2.6.2 at commit
`819ab74f851ee168504cbc392ec32e7bed1d82e9` and its pinned quictls submodule, plus
checksum-verified Mbed TLS. It cross-compiles ARM64 static libraries and reuses
that TLS build for Beam's EVP/X.509 operations. Normal CMake configuration downloads
no native dependencies. Android imports the ARM64 quictls archives directly;
host OpenSSL/pkg-config metadata cannot add libraries to the Android link.
All generated output and local SDK configuration stay ignored.

The APK contains only `arm64-v8a`; other ABI overrides are rejected. Debug signing
is automatic; release signing/distribution is outside this milestone. Native libraries use
16 KiB page alignment, including the NDK's packaged libc++ runtime. No desktop Qt,
Swift, Bonjour/Avahi daemon, or Tailscale executable is linked into the APK.

## Install on your phone

The usual development route is [ADB](https://developer.android.com/tools/adb),
included in the SDK's `platform-tools` package:

1. Enable **Developer options → USB debugging** on your phone.
2. Connect it with a USB data cable, unlock it, and approve the computer's USB
   debugging prompt.
3. From the repository root, run:

   ```sh
   make android-install
   ```

This builds the debug APK, detects authorized devices with `adb devices`, and
installs with `adb install -r`, preserving app data. It stops when zero or multiple
devices are authorized. To choose a phone or emulator explicitly:

```sh
"$ANDROID_HOME/platform-tools/adb" devices
make android-install ANDROID_SERIAL=YOUR_DEVICE_SERIAL
```

`ADB` overrides the executable; otherwise it uses `$ANDROID_HOME/platform-tools/adb`
or `adb` on `PATH`. The installer also checks `ANDROID_SDK_ROOT` and the
project-local `build/android-tools/sdk` directory, so this existing local SDK
does not require shell exports for APK installation. A missing ADB reports setup
instructions. To install an existing APK without rebuilding, use
`make android-install-apk`, optionally with `ANDROID_APK=/absolute/path/to/app.apk`.
Install errors are returned unchanged; the command never uninstalls an app to
bypass a signing-key mismatch, which would erase its data.

A cable is optional: Android 11+ supports wireless debugging after ADB pairing
on the same Wi-Fi network. Follow the [wireless setup](https://developer.android.com/tools/adb#wireless-android11-command-line),
then use the same Make command. You can also copy
`android/app/build/outputs/apk/debug/app-debug.apk` to your phone and open it in
the phone's file manager, approving installation from that source. GitHub CI's
`beam-android` artifact contains this APK too. Neither ADB nor a computer is
needed for that manual APK installation.

## Pair and choose a device

Choose **Manage devices → Add device** on Android and **Add device** on the other
device. Scan the computer's QR, select a nearby device and enter its PIN, or paste
its invitation. Approve on
both screens. Android discovers/advertises `_beam-pair._tcp` through NsdManager
only while this screen is open. The bundled ZXing decoder works offline through
CameraX; Google Play Services are not required. Camera denial keeps paste/PIN
available. Leaving the screen cancels pairing; recreation starts a new invitation.
Transfers pause during setup and resume on return when previously enabled;
explicit Stop remains respected. Full certificates and routes are saved for you.

The following manual PEM workflow remains available under **Manage devices and
identity**:


1. Open **Manage devices → Show / export my public certificate**.
   Save `beam-android.pem` through the system document picker and transfer it to
   the desktop. Never export/copy Android's private identity store.
2. On the desktop, run `beam identity init`, `beam identity show`, and
   `beam identity export > desktop.pem`. Exchange only the exported public PEMs.
3. Independently compare each full 64-character SHA-256 fingerprint with the
   identity screen on its original device.
4. On Android, choose **Pair a device**, import/paste `desktop.pem`, enter a
   local alias, the independently verified desktop fingerprint, and its numeric
   address, such as `192.168.1.20:4269` or `[IPv6]:4269`.
5. On desktop, approve Android's public certificate and verified fingerprint:

   ```sh
   ./build/beam pair phone --cert beam-android.pem \
     --fingerprint VERIFIED_ANDROID_SHA256 --endpoint 192.168.1.30:4269
   ```

The paired-device dropdown lists approved aliases. **Update device address**
changes routing while retaining the pinned certificate. Stop active transfers
before updating/removing devices. Pairing must be reciprocal. Empty trust fails
closed, and system roots/discovery records cannot grant authorization.
See [pairing](pairing.md) for certificate/storage rules and renewal behavior.

Transfers use saved numeric endpoints. Pairing LAN discovery is available;
transfer-receiver DNS-SD and Tailscale CLI discovery remain disabled. A manually entered numeric tailnet
address works when the Android Tailscale VPN and peer routing permit UDP; this
does not change Beam's certificate verification.

## Send and receive

The main screen contains **Send** and **Receive** cards, with **Manage devices**
and **Settings** at the top. Pairing and connection checks live in Manage devices.
All three screens share cards, controls, spacing, and light/dark colors. Idle
progress bars and empty received-text fields stay hidden. File names appear only
after choosing a file; pasted invitations expand on demand in Add device.

Choose a paired device and **Choose file**, edit/review its portable ASCII
basename if needed, and tap **Send**. A system **Share → Beam** action selects
one file or text for the same review screen. URLs use the Text choice and the same 64 KiB UTF-8 limit as other text.
No share intent sends automatically. Clipboard writes require the explicit Copy button; Beam never reads the clipboard.

Provider files are streamed into unique private staging directories with 64 KiB
buffers and a 4 GiB limit on actual bytes, including providers without reliable
size metadata. Staging creates a stable snapshot for hashing and transfer. Disk,
grant, unsupported-name, and cancellation failures are displayed without sending
partial selections. **Cancel send** interrupts preparation or the native worker.

Opening Beam starts reception on UDP 4269 once a device is paired and required
permissions are granted. On first use, pair a device in **Manage devices and
identity**; reception starts after pairing without another button press. A share
intent only opens the review screen and does not start reception. **Stop receiving**
pauses reception for the current app session, including activity recreation;
**Start receiving** resumes it or retries after a startup failure. Files are
verified in temporary private staging, then saved automatically to the device's
shared **Downloads** folder through [Android's MediaStore](https://developer.android.com/training/data-storage/shared/media).
Downloads with the same
name get a new name; existing files are never replaced. Incomplete copies stay
hidden until saving finishes. The temporary private copy is removed after a
successful save. No storage permission or manual export is needed. Text
(including URLs) stays in bounded memory and appears in the app's **Received text**
area after verification and acknowledgement; no text file is saved. Both outgoing
and received text have an explicit **Copy** button at the top right.
The **Receive** card shows transfer percentage, speed,
verification, and saving progress. A successful save appears for eight seconds
and clears when the main screen reopens. Completion messages stay in service
memory; they are not saved to preferences or restored after restart. Upgrading
removes the old `settings / lastReceivedFile` preference.
**Downloads** opens the system downloads view. A separate completion
notification announces received files/text when notification permission is enabled;
its tap opens Beam and never opens received content automatically. In-app feedback
works with notification permission denied. If saving to Downloads fails, Beam
reports the error and retains the verified private copy. **Retry saving** appears
only when such files exist and retries publication to Downloads; this includes
files retained by older versions. Beam never opens content or copies it to the
clipboard automatically. If acknowledgement fails after publication,
a verified file can remain; check received files before retrying.

Transfers/reception run in a `connectedDevice` foreground service started while
the app is visible, with an ongoing **Stop** notification. This keeps active
workers alive across activity recreation/backgrounding; Stop cancels both
directions. Notification permission is optional on Android 13+ (the system still manages foreground-service
visibility when denied). Idle services stop. After process death/reboot, reception
waits until the user opens Beam again. Startup removes abandoned outgoing staging;
the core keeps partial incoming files hidden. Android may still terminate a process, and OEM
power/network policies can affect background reachability.

The manifest requests network access, the connected-device service permission,
and `CHANGE_NETWORK_STATE`, an Android prerequisite for this foreground-service
type. It requests no broad storage/media access. Android 17+ requires explicit
local-network permission, requested before automatic reception or an explicit
send/receive retry; denial leaves the operation stopped with an explanation.
Grant the permission in app settings if needed, then retry. Optional notification
permission is requested alongside the first network operation; denying it does
not block reception when local-network access is allowed. Older Android versions
use `INTERNET` for LAN access. See the official
[foreground-service requirements](https://developer.android.com/develop/background-work/services/fgs/service-types#connected-device),
[document access](https://developer.android.com/training/data-storage/shared/documents-files),
and [local-network permissions](https://developer.android.com/privacy-and-security/local-network-permission).

The app shows send/receive percentage, progress bars, and average payload MiB/s;
the foreground notification reflects progress too. Completion still follows
verification and acknowledgement. **Manage devices → Check** checks the selected
peer's authenticated Beam receiver with a bounded probe and no payload/file.
**Settings** shows Downloads as the receive destination and stores the listen port and timeout;
see [configuration](configuration.md). Stop active workers before editing.
Beam 0.9 requires protocol v4 on both peers; existing identity/pairing remains usable.

## Validation

```sh
make check                    # includes native Android session/paired-transfer tests
make android-format           # ktfmt for Kotlin and Gradle scripts
make android-check            # ktfmt check, Android lint, JUnit, ARM64 debug APK
make android-native-test      # ARM64 link graph with incompatible host OpenSSL metadata
make android-connected-test   # requires a running ARM64 emulator or attached ARM64 device
```

`android-native-test` needs pkgconf (`brew install pkgconf` or your distribution's
`pkgconf` package). It configures ARM64 with host metadata advertising
`libjitterentropy` and `zstd`, and rejects either dependency in the generated
Android link graph. CI runs it before building the APK.

Native session tests cover invalid commands, byte bounds/NULs, unsupported kinds,
fingerprint rejection, authenticated file/text transfers, exact Unicode bytes,
trust edits during reception, revocation, and persistent identity. JVM tests
exercise provider-name/URI validation, UTF-8 limits, bounded streaming, and
cancellation. AndroidX Test Runner 1.7.0 and JUnit are test-only dependencies.
Device tests exercise actual JNI paired transfers, shared Downloads publication,
duplicate names, failed-copy cleanup, zero-byte files, background receipt,
receive UI/completion notifications, automatic reception on launch,
Stop across activity/service recreation, provider staging/foreground controls,
and share-target review without automatic staging/sending. The launch test also
supports Android 17 permission allow/deny cases with optional notifications denied.
Linux/Android storage uses search-only ancestor handles and atomic no-replace
rename, retaining
symlink rejection and collision protection in the app sandbox. Main-branch Android
CI builds only ARM64, checks Kotlin/lint/JUnit, and uploads the debug APK. Its accelerated Linux emulator uses an Intel system image with
ARM64 translation; CI verifies ARM64 ABI availability before running the JNI/UI
tests against the ARM64 APK. Local device tests should use an ARM64 emulator or
phone. Set `ANDROID_SERIAL` when multiple devices are attached.

Physical-device LAN/Tailscale interoperability, OEM background policy, notification
delivery, and third-party document-provider behavior require device checks; emulator
and host tests do not establish those behaviors.

## Device management

**Manage devices** opens a native screen with search, device cards, saved routes,
connection checks, refresh, Add device, and My identity. Edit a device's local name,
IP/port, and optional Tailscale route together. Enable **Replace certificate** to
choose/paste a new public certificate and verify its full SHA-256 fingerprint.
Invalid edits keep the form open. **More** shows the certificate or confirms unpair.

**Export backup** uses the system Create document picker. **Import backup** reads
at most 2 MiB through a bounded provider stream, validates in C++, and previews all
entries before **Restore devices** adds trust. Backup files contain no private keys.
An Android reinstall changes the local identity: importing the device list then
requires pairing the new identity on the other devices too. Existing devices keep
their current settings. The service invalidates staged outgoing sends and joins
native workers for management changes; reception resumes if it was enabled, while
an explicit Stop remains in effect. See [pairing](pairing.md#device-management).
