# macOS icon research

Checked 2026-10-17 following rejection of the first three Beam concepts.

## Correction to the first exploration

The generated boards changed the wallpaper behind essentially unchanged icon
artwork. Their Light/Dark captions showed background contrast, not native icon
appearance variants. No production icon integration was implemented.

The next design round must distinguish three separate things: the app's window
appearance, the system's app-icon style, and the monochrome menu-bar mark.

## What other apps do

| App | Evidence checked | Finding |
| --- | --- | --- |
| Things | Developer's [OS 26 announcement](https://culturedcode.com/things/blog/2025/09/things-for-os-26/) | Retains its recognizable blue-box identity and supplies Default, Dark, Tinted, and Clear variants. |
| Ghostty 1.3.1 | Installed `Info.plist` and `Assets.car` | Declares the `Ghostty` icon; the catalog contains five-layer `Ghostty.iconstack` entries for Aqua, DarkAqua, and Tintable appearances. A `Ghostty.icns` resource is also present. |
| Safari 27.0.1 | Installed `Info.plist` and `Assets.car` | Declares `AppIconUpdated`; its catalog likewise contains five-layer stacks for Aqua, DarkAqua, and Tintable appearances, alongside an `.icns` resource. |
| Zed 1.23.3 | Installed `Info.plist` and resource directory | Declares the traditional `Zed.icns` resource. This demonstrates a different packaging path, not evidence that the app authors supplied native dark/clear variants. |

Installed-app observations are a local snapshot on macOS 27.0.1, not claims about
every release. They establish bundled resources, not the exact Dock rendering
under every system setting. No application settings were changed. The Things
announcement is direct developer evidence; no inference about its internal build
pipeline is needed.

For reproducibility, the local inspection used Python's standard-library
`plistlib` to read `/Applications/APP.app/Contents/Info.plist`, directory listings
of `Contents/Resources`, and:

```sh
assetutil --info /Applications/Ghostty.app/Contents/Resources/Assets.car
assetutil --info /Applications/Safari.app/Contents/Resources/Assets.car
```

The relevant top-level catalog entries have `AssetType: IconImageStack`,
`LayerCount: 5`, and appearances `NSAppearanceNameAqua`,
`NSAppearanceNameDarkAqua`, and `ISAppearanceTintable`. These are inspection
details, not APIs Beam should call.

## Platform behavior

Apple documents window appearance and icon/widget style as separate settings.
Icon style can be Default, Dark, Clear, or Tinted; Dark can remain enabled or
follow the system appearance. Therefore, dark wallpaper, Dark Mode windows, and
dark app-icon rendering are not interchangeable tests. See
[Appearance settings](https://support.apple.com/guide/mac-help/change-appearance-settings-mchlp1225/26/mac/26).

Apple's current workflow uses layered artwork in
[Icon Composer](https://developer.apple.com/icon-composer/). The
[app-icon guidelines](https://developer.apple.com/design/human-interface-guidelines/app-icons/)
recommend consistent recognizable geometry across styles, unmasked source
layers, and system-rendered visual effects. The original boards' baked bevels,
glow, lighting, and perspective should not be copied wholesale into those layers.
Appearance-specific color/material adjustments should be previewed through the
native tools.

Traditional full-color assets can remain suitable in both light and dark window
appearances; a separate recoloring is not automatically necessary. Apple's
[Dark Mode guidance](https://developer.apple.com/design/human-interface-guidelines/dark-mode)
explicitly permits reusing an asset that works in both. A traditional `.icns`
alone does not establish that author-controlled modern appearance variants exist.

## Revised approach for Beam

1. Start again with an original, clear graphic symbol. Judge silhouette and
   optical balance at Dock size before adding material effects. The rejected
   glass bridge, ribbon B, and portals are not the default direction.
2. Author separate vector foreground/background layers. Use the Icon Composer
   workflow for supported modern macOS versions and retain a tested `.icns`
   fallback for Beam's macOS 13+ baseline. Build integration must compile and
   package the asset, not merely copy a presentation PNG into the bundle.
3. Review actual Default, Dark, Clear, and Tinted renders of the same identity.
   Compare artwork variants on the same background first; then test wallpaper
   contrast. Let the system select the icon style rather than adding an
   app-specific icon theme switch or tying Dock artwork directly to window theme.
4. Design a separate small [template image](https://developer.apple.com/documentation/appkit/nsimage/istemplate)
   for the macOS menu bar. Reuse the
   identity in Android adaptive/themed layers without copying Mac materials or
   its rounded enclosure into Android source artwork.
5. Keep presentation art labeled as a concept until native preview/build checks
   pass. Check system rendering in the installed app, including older-macOS
   fallback behavior, before claiming appearance support.

The current host selects `/Library/Developer/CommandLineTools`; `xcrun --find
actool` reports that the asset compiler is unavailable. A compatible Apple asset
toolchain is needed to validate production layered-icon compilation and native
previews. This research does not claim that those checks have passed.
