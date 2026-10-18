# macOS build, install, and cleanup

Development uses `org.beam.desktop.dev`; production keeps `org.beam.desktop` for
upgrades. These have separate notification and preference identities. They still
share CLI identity/pairing data unless launched with different `--state-dir`
paths. Simultaneous receivers also need different ports and output folders.

## Build and install

After `make deps`, with full Xcode selected and Ninja installed:

```sh
make desktop                          # Debug: build/macos/dev
open build/macos/dev/beam-desktop.app

cmake --preset macos-dev
cmake --build --preset macos-dev
cmake --preset macos-release
cmake --build --preset macos-release

make install                          # Release CLI and ~/Applications/Beam.app
# Install only the already-built application:
./scripts/install-macos.sh --user
./scripts/install-macos.sh --development --user   # ~/Applications/Beam Dev.app
```

`DESKTOP_BUILD_DIR` and `INSTALL_BUILD_DIR` still override Make's build paths.
Old build directories are not moved or deleted. `make clean
BUILD_DIR=build/macos/dev` cleans compiled targets in that tree, preserving project
data. CLI and Linux build defaults are unchanged.

The CMake cache option `BEAM_MACOS_BUNDLE_ID` accepts `org.beam.desktop` or a
sub-identifier, such as `org.beam.desktop.experiment`. An empty value selects
production for a single-config Release build without sanitizer flags; other
configurations default to development. Multi-config generators default to
development; explicitly select the intended identifier for distribution.
Presets select identifiers explicitly. Select the development identifier for
sanitizer builds, including when overriding the Release preset. Generated
`Info.plist` and target bundle metadata use the same resolved identifier.

The public CMake `desktop` install component and Make use the install script.
It prepares one fresh bundle in a hidden sibling directory, bundles dependencies,
applies the existing local ad-hoc signatures, verifies the finished app, and
replaces the selected installation. These are local builds, not notarized
distribution packages. The internal `desktop-staging` component only prepares
the bundle. Its executable permissions and finished signatures survive the move.
Upgrades validate the old app's exact identifier and executable and gracefully
stop that copy before replacement. Obsolete files disappear with the old tree.
Other copies and registrations are left alone. Failed preparation retains the
old app; failed publication attempts rollback and retains a recovery copy if
rollback fails. Successful installs remove temporary copies.

Use `--applications-dir "/absolute/path with spaces"` for a custom destination
or `--system` for `/Applications`. The scripts never invoke `sudo`; the selected
destination must be writable. User-local installs run as your own user.
`--build-dir`, `--config`, and `--bundle-id` select custom builds. The script
installs only the app; `make install` also installs the CLI/runtime.
`--no-register` skips optional registration of the installed app.
CMake `DESTDIR` staging skips termination and Launch Services changes. Nothing
starts Beam automatically.

## Uninstall

```sh
make uninstall                        # CLI/runtime and production app; data kept
./scripts/uninstall-macos.sh --user    # app only; data kept
./scripts/uninstall-macos.sh --development --user
./scripts/uninstall-macos.sh --system
./scripts/uninstall-macos.sh --user --clear-notifications
```

Repeat custom Make `APPLICATIONS_DIR`/`INSTALL_PREFIX` overrides, or script
`--applications-dir`/`--bundle-id` options. For a development Make installation,
use `MACOS_BUNDLE_ID=org.beam.desktop.dev` on both install and uninstall.

Uninstall validates the bundle, then uses public AppKit APIs to terminate only
apps matching both its physical path and identifier. A quit failure aborts file
removal; it never kills by process name. Beam currently creates no launch agents,
login items, or background helpers. Transfer workers join when the app quits;
unrelated login items and launch agents are left alone. Targeted Launch Services
unregistration is attempted before removing the selected app. Repeating removal,
including after deletion of the app, is safe. Bundle symlinks are unlinked without
following their targets. Development bundles are preserved by production removal.

By default, preferences, caches, configuration, private identities, paired devices,
and received files are retained. Optional data removal requires a typed `DELETE`
confirmation (or explicit `--yes-delete-data` for noninteractive use):

```sh
./scripts/uninstall-macos.sh --user --purge-data
```

This removes the selected identifier's preferences, caches, saved application
state, and identifier-named Application Support directory, **plus the shared**
`${XDG_CONFIG_HOME:-$HOME/.config}/beam` identity/pairing/configuration directory.
The shared directory also belongs to CLI and development builds. Quit all copies
and back up needed private identities before choosing this option. Nondefault
`--state-dir` stores and received files are never removed. Data removal refuses
symlink ancestors and cannot run as root; leaf symlinks are unlinked.

## Notifications

macOS transfer notifications default to off. Enable **Transfer notifications** in
Beam's application menu to request OS authorization. Startup, headless tests, and
transfer results never trigger a permission prompt. In-app results remain
available with notifications off. The preference and authorization are independent
for development and production. Existing OS authorization is retained across
upgrades; the new explicit feature opt-in defaults to off.

`--clear-notifications` executes the installed app's bundled executable before
removal, keeping its notification identity. It uses
`UNUserNotificationCenter.current()` to clear pending/delivered notifications,
checks both lists are empty, and exits without starting reception or reading
configuration. It never requests permission. Timeouts, failures, and older apps
without the command are reported as best effort and do not prevent removal.
The command can also be run directly on the bundle executable.

Apple's public [User Notifications API](https://developer.apple.com/documentation/usernotifications/unusernotificationcenter)
supports removing pending/delivered notifications. It provides no documented
operation guaranteeing removal of saved authorization or the System Settings >
Notifications entry. macOS may retain that entry after uninstall; Launch Services
unregistration does not reset notification settings. No Notification Center
database, other app's notifications, or system service is modified.

### Remove a retained entry or stale notification icon

macOS provides a manual reset in System Settings even though apps cannot invoke
an equivalent public API:

1. Open **System Settings → Notifications**.
2. Control-click (or right-click) **Beam** in Application Notifications.
3. Choose **Reset Notifications…**, then confirm **Reset Notifications**.

This resets only Beam's notification permission. On the development Mac it
removed the retained Beam entry after uninstall; other apps' entries and settings
stayed in place. Apply the same action separately to Beam Dev if needed. Beam
will have to request notification permission again if you later enable its
**Transfer notifications** feature. This is a user action, not something the
uninstaller automates.

If an installed Beam shows an old icon, first inspect and unregister old build
copies with `./scripts/cleanup-macos.sh --development`, then install the current
production build with `make install`. Installation validates the icon resources
and requests registration of that exact installed bundle. If the notification
icon remains stale, reset Beam's entry using the steps above. Build copies can be
rediscovered, and macOS controls its icon caches; re-registration alone does not
guarantee a refresh. Uninstall does not delete old build directories.

## Inspect development registrations

```sh
make macos-cleanup                     # inspection only
./scripts/cleanup-macos.sh --dry-run
./scripts/cleanup-macos.sh --development
./scripts/cleanup-macos.sh --development --root "/absolute/build directory"
```

Inspection lists bundles with the exact production/development identifiers inside
the selected repository/build root and detects missing paths in recognizable
Launch Services bundle records. `--development` requests unregistration of those
paths, including legacy production-identity build artifacts. It preserves builds,
installed apps outside the selected root, and unrelated registrations. It rejects
ambiguous paths and identifier prefixes. It never resets the global database.
`lsregister` is an internal macOS utility: its availability, dump format, and
stale-record removal are best effort. A successful request does not guarantee a
stale record disappears. Existing bundles can be rediscovered by macOS; directory
layout alone cannot prevent discovery. Remove unwanted build directories yourself
only after reviewing their contents.

## Verification

`make check BUILD_DIR=build/macos/dev CMAKE_ARGS='-G Ninja -DBEAM_ENABLE_DESKTOP=ON'`
includes signed install/replacement, obsolete-file removal, repeated uninstall,
coexisting identifiers, paths with spaces, symlink protection, invalid destinations
and identities, failed staging, data-deletion confirmation, exact registration
matching, and missing-bundle records. Install smoke tests use `DESTDIR` and an exit
trap, so failed assertions also remove temporary bundles. Fixtures use hidden
directories and attempt targeted unregistration before deletion: macOS may still
discover test bundles even without explicit registration. UI tests check notification
opt-in. Existing native icon, signature,
relocation, CLI, and core checks still run.
The bundle-identity check also configures real Debug, Release, RelWithDebInfo,
sanitizer, and custom-identifier fixtures, then removes those build directories.

Interactive OS verification: install both identifiers into a dedicated user-local
Applications test destination, launch with separate state directories and no
discovery, uninstall one while running, and confirm the other stays open. Opt in
only when testing delivery, then clear notifications through that bundle before
uninstall. System Settings may retain either entry. Use a separate test app to
check its notification state stays unchanged. OS authorization checks require an
interactive user session.

`bash tests/macos_live_lifecycle.sh` performs the isolated user-local launch and
uninstall cycles after both presets have been built. It checks that development
stays open while production is removed, executes each bundle's notification
cleanup command, and removes its test folder. It does not enable notifications
or test delivery banners; those require explicit OS authorization.
