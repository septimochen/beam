# Android

Beam's Android app sends and receives the same authenticated `beam/3` file,
text, and URL transfers as the CLI and desktop apps. It supports Android 10+
(API 29), with ARM64 device and x86-64 emulator APK libraries. The app uses Kotlin
and JNI over the existing C++ identity store and joined transfer workers.

## Build

Install JDK 17+, Android command-line tools (or Android Studio), CMake 3.25+,
Ninja, Git, Perl, and Make. Set `JAVA_HOME` and `ANDROID_HOME` for those installs.
Install the pinned SDK packages:

```sh
sdkmanager 'platforms;android-37.0' 'build-tools;36.0.0' \
  'ndk;28.2.13676358' 'cmake;3.31.6' 'platform-tools'
export ANDROID_NDK="$ANDROID_HOME/ndk/28.2.13676358"
make android-deps ANDROID_ABI=arm64-v8a
make android-deps ANDROID_ABI=x86_64
make android
```

The SDK's newer Android CLI can also install these packages; follow its `android
sdk --help`. The checked-in Gradle 9.8.1 wrapper validates its distribution SHA-256;
AGP 9.2.0 provides built-in Kotlin. `make android-deps` explicitly downloads
MsQuic v2.6.2 at commit `819ab74f851ee168504cbc392ec32e7bed1d82e9` and its pinned
quictls submodule. It cross-compiles a static library per ABI and reuses that TLS
build for Beam's EVP/X.509 operations. Normal CMake configuration downloads no
native dependencies. Android imports the ABI-specific quictls archives directly;
host OpenSSL/pkg-config metadata cannot add libraries to the Android link.
All generated output and local SDK configuration stay ignored.

To build one architecture, provision that dependency and run
`make android ANDROID_GRADLE_ARGS=-PbeamAbis=arm64-v8a`. Debug signing is automatic;
release signing/distribution is outside this milestone. Native libraries use
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

1. Open **Manage devices and identity → Show / export my public certificate**.
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

This milestone uses saved numeric endpoints. Android LAN browsing/advertising
and Tailscale CLI discovery are disabled. A manually entered numeric tailnet
address works when the Android Tailscale VPN and peer routing permit UDP; this
does not change Beam's certificate verification.

## Send and receive

Choose a paired device and **Choose file**, edit/review its portable ASCII
basename if needed, and tap **Send**. A system **Share → Beam** action selects
one file or text for the same review screen. Text and HTTP(S) URLs have explicit
choices; their UTF-8 byte limits and validation match the core (64 KiB / 8 KiB).
No share intent sends automatically. There is no clipboard access.

Provider files are streamed into unique private staging directories with 64 KiB
buffers and a 4 GiB limit on actual bytes, including providers without reliable
size metadata. Staging creates a stable snapshot for hashing and transfer. Disk,
grant, unsupported-name, and cancellation failures are displayed without sending
partial selections. **Cancel send** interrupts preparation or the native worker.

Tap **Start receiving** to listen on UDP 4269 for paired devices. Files, text,
and URLs are verified before exclusive publication in Beam's private `received`
directory. Existing files are never replaced. **Save a received file…** explicitly
exports one completed file through the document picker. Beam never opens content
or installs it into the clipboard. If acknowledgement fails after publication,
a verified file can remain; check received files before retrying.

Transfers/reception run in a user-started `connectedDevice` foreground service
with an ongoing **Stop** notification. This keeps active workers alive across
activity recreation/backgrounding; Stop cancels both directions. Notification
permission is optional on Android 13+ (the system still manages foreground-service
visibility when denied). Idle services stop, and reception never restarts after
process death/reboot. Startup removes abandoned outgoing staging; the core keeps
partial incoming files hidden. Android may still terminate a process, and OEM
power/network policies can affect background reachability.

The manifest requests network access, the connected-device service permission,
and `CHANGE_NETWORK_STATE`, an Android prerequisite for this foreground-service
type. It requests no broad storage/media access. Android 17+ requires explicit
local-network permission, requested when starting a send/receiver; denial leaves
the operation stopped with an explanation. Older Android versions use `INTERNET`
for LAN access. See the official
[foreground-service requirements](https://developer.android.com/develop/background-work/services/fgs/service-types#connected-device),
[document access](https://developer.android.com/training/data-storage/shared/documents-files),
and [local-network permissions](https://developer.android.com/privacy-and-security/local-network-permission).

## Validation

```sh
make check                    # includes native Android session/paired-transfer tests
make android-format           # ktfmt for Kotlin and Gradle scripts
make android-check            # ktfmt check, Android lint, JUnit, both-ABI debug APK
make android-native-test      # both-ABI link graphs with incompatible host OpenSSL metadata
make android-connected-test   # requires a running emulator or attached device
```

`android-native-test` needs pkgconf (`brew install pkgconf` or your distribution's
`pkgconf` package). It configures both ABIs with host metadata advertising
`libjitterentropy` and `zstd`, and rejects either dependency in the generated
Android link graph. CI runs it before building the APK.

Native session tests cover invalid commands, byte bounds/NULs, unsupported kinds,
fingerprint rejection, authenticated file/text/URL transfers, exact Unicode bytes,
trust edits during reception, revocation, and persistent identity. JVM tests
exercise provider-name/URI validation, UTF-8 limits, bounded streaming, and
cancellation. AndroidX Test Runner 1.7.0 and JUnit are test-only dependencies.
Device tests exercise actual JNI paired transfers, provider staging/foreground
controls, and share-target review without automatic staging/sending. Linux/Android
storage uses search-only ancestor handles and atomic no-replace rename, retaining
symlink rejection and collision protection in the app sandbox. Main-branch Android CI builds both
ABIs, checks Kotlin/lint/JUnit, runs x86-64 emulator tests, and uploads the debug APK.

Physical-device LAN/Tailscale interoperability, OEM background policy, notification
delivery, and third-party document-provider behavior require device checks; emulator
and host tests do not establish those behaviors.
