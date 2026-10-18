#!/bin/bash
set -euo pipefail
source_dir=$1
build=$2
config=$3
id=$4
root=$(mktemp -d "$build/.beam lifecycle paths with spaces.XXXXXXXX")
cleanup() {
    env -u BEAM_LSREGISTER "$source_dir/scripts/cleanup-macos.sh" --development --root "$root" >/dev/null 2>&1 || true
    rm -rf -- "$root"
}
trap cleanup EXIT
apps="$root/Applications with spaces"
scripts="$source_dir/scripts"
name=Beam.app
[[ $id != org.beam.desktop.dev ]] || name='Beam Dev.app'
install_app() {
    "$scripts/install-macos.sh" --applications-dir "$apps" --build-dir "$build" --config "$config" --bundle-id "$id" --staging
}
remove_app() {
    "$scripts/uninstall-macos.sh" --applications-dir "$apps" --bundle-id "$id" --staging
}
reject() {
    if "$@" > "$root/rejected.log" 2>&1; then echo "Unexpected success: $*" >&2; exit 1; fi
}
install_app
echo old > "$apps/$name/obsolete-resource"
install_app
[[ ! -e $apps/$name/obsolete-resource ]]
codesign --verify --deep --strict "$apps/$name"
[[ -z $(find "$apps" -maxdepth 1 -name '.beam-install.*' -print) ]]

# Another identity and an unrelated application survive replacement/removal.
other=org.beam.desktop
other_name=Beam.app
if [[ $id == org.beam.desktop ]]; then other=org.beam.desktop.dev; other_name='Beam Dev.app'; fi
ditto "$apps/$name" "$apps/$other_name"
plutil -replace CFBundleIdentifier -string "$other" "$apps/$other_name/Contents/Info.plist"
codesign --force --sign - --timestamp=none "$apps/$other_name"
mkdir -p "$apps/Other.app"
echo keep > "$apps/Other.app/protected"
remove_app
remove_app
[[ -f $apps/$other_name/Contents/Info.plist && $(cat "$apps/Other.app/protected") == keep ]]
install_app
remove_app

# Identity mismatches, symlinks, bad paths and failed staging never remove data.
mkdir -p "$apps/$name"
echo keep > "$apps/$name/protected"
reject install_app
reject remove_app
[[ $(cat "$apps/$name/protected") == keep ]]
rm -rf "$apps/$name"
ln -s "$apps/Other.app" "$apps/$name"
reject install_app
remove_app
[[ -f $apps/Other.app/protected ]]
reject "$scripts/install-macos.sh" --applications-dir / --build-dir "$build"
reject "$scripts/install-macos.sh" --applications-dir "$apps" --build-dir "$root/missing build"
[[ -z $(find "$apps" -maxdepth 1 -name '.beam-install.*' -print) ]]
reject "$scripts/uninstall-macos.sh" --applications-dir "$apps" --bundle-id "$id" --purge-data < /dev/null

# Exercise the real cleanup parser with an isolated Launch Services double.
export BEAM_LSREGISTER="$root/lsregister"
export BEAM_TEST_RECORDS="$root/records"
export BEAM_TEST_LOG="$root/unregistered"
cat > "$BEAM_LSREGISTER" <<'SH'
#!/bin/bash
if [[ $1 == -dump ]]; then cat "$BEAM_TEST_RECORDS"; else printf '%s\n' "$2" >> "$BEAM_TEST_LOG"; fi
SH
chmod +x "$BEAM_LSREGISTER"
missing="$root/deleted build/Beam.app"
cat > "$BEAM_TEST_RECORDS" <<EOF
--------------------------------------------------------------------------------
bundle id: Beam (0x123)
path: $missing (0xabcd)
identifier: org.beam.desktop.dev
--------------------------------------------------------------------------------
bundle id: Other (0x124)
path: $root/Other missing.app (0xabce)
identifier: org.beam.desktop.dev.other
--------------------------------------------------------------------------------
bundle id: Beam elsewhere (0x125)
path: /unrelated/Beam.app (0xabcf)
identifier: org.beam.desktop
--------------------------------------------------------------------------------
bundle id: Non-bundle record (0x126)
path: $root/../escape.app (0xabd0)
identifier: org.beam.desktop
--------------------------------------------------------------------------------
EOF
"$scripts/cleanup-macos.sh" --root "$root" --dry-run > "$root/inspection"
[[ ! -e $BEAM_TEST_LOG ]]
"$scripts/cleanup-macos.sh" --root "$root" --dry-run --development > /dev/null
[[ ! -e $BEAM_TEST_LOG ]]
grep -F "Stale registration (missing bundle): $missing" "$root/inspection"
"$scripts/cleanup-macos.sh" --root "$root" --development
grep -Fx "$apps/$other_name" "$BEAM_TEST_LOG"
grep -Fx "$missing" "$BEAM_TEST_LOG"
[[ $(wc -l < "$BEAM_TEST_LOG" | tr -d ' ') == 2 ]]
[[ -f $apps/$other_name/Contents/Info.plist && -f $apps/Other.app/protected ]]
"$scripts/cleanup-macos.sh" --root "$root" --development
echo 'macOS lifecycle tests passed.'
