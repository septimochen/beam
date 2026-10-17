# Beam icons

The three forward rays are an original vector mark for Beam's direct sharing.
The mark has a quiet blue/violet palette, generous internal separation, and no
painted glass, cast shadow, or simulated hardware. Platform materials are applied
by the system when supported.

`tools/GenerateIcons.swift` is the geometric source of truth. On macOS, run
`make icons` to regenerate the committed exports using AppKit/Core Graphics and
Apple's `iconutil`; these are system tools, not new package dependencies.

| Resource | Consumer |
| --- | --- |
| `Beam.icon/` | Editable Icon Composer document with separate ray layers, Default/Dark fills, and system-generated mono styles |
| `Beam.icns` | Companion resource for macOS icon readers |
| `BeamTemplate.pdf` | Separate monochrome macOS menu-bar template |
| `beam.svg`, `beam.png` | Linux installed launcher and embedded Qt window/tray icon |
| `beam-dark.png` | Flat dark color study; not a native macOS rendering |
| `beam-symbolic.svg` | Monochrome vector reference |
| Android `res/drawable/ic_beam_*.xml` | Adaptive foreground, themed monochrome, and notification artwork |
| Android `res/values*/icon_colors.xml` | Opaque day/night backgrounds and foreground colors |

Android's foreground is scaled into the centered adaptive-icon safe area; the
system applies circle/squircle masks. The notification mark has no background.

## Native macOS appearances

`make desktop` always compiles `Beam.icon` into `Assets.car`
using `actool` and packages it alongside the `.icns` and menu-bar PDF. The system
selects Default, Dark, Clear, and Tinted rendering; Beam does not observe window
theme changes to swap Dock artwork.

Full Xcode is required: Command Line Tools do not include `actool`, and desktop
configuration fails with setup instructions when the compiler is missing.
Native icon compilation needs no extra build arguments and cannot be disabled.
For a custom toolchain location, set `DEVELOPER_DIR` to its Xcode developer
directory or configure with `-DBEAM_ACTOOL=/path/to/actool`.

If Xcode is installed but `xcode-select -p` still reports
`/Library/Developer/CommandLineTools`, select Xcode for the current shell:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
xcrun --find actool
make desktop
```

`DEVELOPER_DIR` does not change the system-wide developer directory. To make
Xcode the system default instead, run
`sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer`.
When switching from a different compiler/SDK, use a fresh build directory with
`make desktop DESKTOP_BUILD_DIR=build-desktop-xcode`.

`make icons-native` compiles just the native catalog into `build/icons/native`.
The focused native-icons GitHub workflow performs that compilation and verifies
the light, dark, and tintable icon stacks, then uploads the catalog as a build
artifact. Production app previews should come from Icon Composer or the packaged
app; the exported flat PNG studies are not substitutes for those previews.
