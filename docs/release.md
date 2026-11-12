# Beam releases

## Install a release

Download the files for your platform from [GitHub Releases](https://github.com/septimochen/beam/releases).
Verify the downloads against `SHA256SUMS` using `sha256sum -c SHA256SUMS`
(Linux) or `shasum -a 256 -c SHA256SUMS` (macOS). If you downloaded only one
asset, compare its digest with its line in `SHA256SUMS`.

### Ubuntu 26.04 x86-64

Install the runtime dependencies. Configure [Microsoft's APT repository](development.md#build-setup)
for `libmsquic` if your system does not already provide a compatible MsQuic 2.x
runtime (2.4 or newer).

```sh
sudo apt install libmsquic libssl3t64 libavahi-compat-libdnssd1 libstdc++6
# For LAN discovery, install and start Avahi.
sudo apt install avahi-daemon
sudo systemctl enable --now avahi-daemon
```

The desktop archive also needs the Qt runtime and a graphical desktop session.

```sh
sudo apt install libqt6dbus6 libqt6widgets6 libqt6quickcontrols2-6 qt6-qpa-plugins \
  qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-layouts \
  qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtqml \
  qml6-module-qtqml-models qml6-module-qtqml-workerscript
```

For a quick start, extract either archive to its own directory and run `bin/beam`
or `bin/beam-desktop` from there. To install system-wide, extract the verified
archive into `/usr/local`. The desktop menu launcher uses that prefix.

```sh
sudo tar -xzf beam-cli-v0.10.0-linux-ubuntu26.04-x86_64.tar.gz -C /usr/local
sudo tar -xzf beam-desktop-v0.10.0-linux-ubuntu26.04-x86_64.tar.gz -C /usr/local
beam --version
```

### Android

Install the signed `beam-android-v0.10.0-arm64-v8a.apk` on an ARM64 device running
Android 10 or later. See [Android installation](android.md#install-on-your-phone)
for sideloading steps and how to handle an existing debug install.

## Cut a release

1. Set the version in [`version.properties`](../version.properties) and increment
   `androidVersionCode` for every subsequent published APK. `0.10.0` starts at code
   `10`. Use a patch bump for fixes and a minor bump for features while Beam is
   pre-1.0. CLI, desktop and APK metadata share the same version. The wire protocol
   has its own version.
2. Commit and push to `main`, then run **Release artifacts** manually on `main`.
   The workflow runs the normal checks, builds the Linux archives, starts the
   extracted desktop app in a clean Ubuntu container, and checks the unsigned APK.
   On Ubuntu 26.04 x86-64 with the [build prerequisites](development.md#build-setup)
   installed, `make package-linux` writes the archives to `build/release/`.
3. After rehearsal, tag the verified `main` commit and push that tag. Use the
   successful **tag** run to download the `beam-linux-v0.10.0` and
   `beam-android-unsigned-v0.10.0` artifacts. Save the separate symbols artifact
   with the release records for crash analysis and build provenance.

   ```sh
   release_commit=REPLACE_WITH_REHEARSED_COMMIT_SHA
   git tag -a v0.10.0 "$release_commit" -m 'Beam 0.10.0'
   git push origin refs/tags/v0.10.0
   tag_run_id=REPLACE_WITH_TAG_RUN_ID
   gh run download "$tag_run_id" -n beam-linux-v0.10.0
   gh run download "$tag_run_id" -n beam-android-unsigned-v0.10.0
   gh run download "$tag_run_id" -n beam-android-symbols-v0.10.0 -D android-symbols
   ```

4. Sign the downloaded APK with your [backed-up local release key](android.md#release-apk).
   Use `apksigner` and `zipalign` from Android SDK Build Tools. Let `apksigner`
   prompt for passwords and verify the certificate against your local signing key.
   Install the signed APK on an ARM64 device and verify pairing and a file transfer
   with the extracted Linux build.

   ```sh
   version=0.10.0
   apk="beam-android-v$version-arm64-v8a.apk"
   zipalign -c -P 16 4 "beam-android-v$version-arm64-v8a-unsigned.apk"
   apksigner sign --ks "$HOME/.android/beam-release/beam-release.p12" \
     --ks-key-alias beam-release --out "$apk" "beam-android-v$version-arm64-v8a-unsigned.apk"
   apksigner verify --verbose --print-certs "$apk"
   adb install -r "$apk"
   ```

5. From the download directory, generate checksums and create a draft with the
   three user downloads. Review the notes and assets in GitHub before publishing.
   Keep released tags fixed and use a new version for subsequent fixes.

   ```sh
   version=0.10.0
   set -- "beam-cli-v$version-linux-ubuntu26.04-x86_64.tar.gz" \
     "beam-desktop-v$version-linux-ubuntu26.04-x86_64.tar.gz" \
     "beam-android-v$version-arm64-v8a.apk"
   shasum -a 256 "$@" > SHA256SUMS
   gh release create "v$version" --verify-tag --draft --title "Beam $version" \
     --generate-notes "$@" SHA256SUMS
   ```
