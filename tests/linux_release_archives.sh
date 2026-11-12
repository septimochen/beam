#!/bin/bash
set -euo pipefail

artifacts=$1
version=$2
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT

for component in cli desktop; do
    mkdir "$root/$component"
    tar -xzf "$artifacts/beam-$component-v$version-linux-ubuntu26.04-x86_64.tar.gz" \
        -C "$root/$component"
    test -s "$root/$component/share/licenses/beam/NOTICE.md"
done

cli="$root/cli/bin/beam"
desktop="$root/desktop/bin/beam-desktop"
test "$("$cli" --version)" = "Beam $version"
test "$(QT_QPA_PLATFORM=offscreen "$desktop" --version)" = "beam-desktop $version"
test ! -e "$root/cli/bin/beam-desktop"
test ! -e "$root/desktop/bin/beam"
for executable in "$cli" "$desktop"; do
    dependencies=$(ldd "$executable")
    grep -qE 'libmsquic\.so\.2 => /' <<< "$dependencies"
    if grep -q 'not found' <<< "$dependencies" ||
        grep -qE '\((RPATH|RUNPATH)\)' <<< "$(readelf -d "$executable")"; then
        echo "Unexpected library resolution for $executable" >&2
        exit 1
    fi
done

entry="$root/desktop/share/applications/beam.desktop"
desktop-file-validate "$entry"
grep -Fxq 'Exec="/usr/local/bin/beam-desktop"' "$entry"
test -s "$root/desktop/share/icons/hicolor/scalable/apps/org.beam.desktop.svg"
test -s "$root/desktop/share/icons/hicolor/512x512/apps/org.beam.desktop.png"

result=0
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_IMPORT_PATH= QML2_IMPORT_PATH= \
    XDG_CONFIG_HOME="$root/config" XDG_DATA_HOME="$root/data" \
    timeout --kill-after=2s 4s dbus-run-session -- "$desktop" --state-dir "$root/state" \
    --output "$root" --no-discovery > "$root/desktop.log" 2>&1 || result=$?
if [[ $result != 124 ]] || grep -Eq 'failed to load component|qrc:/beam/.*(Error|is not installed|unavailable)' "$root/desktop.log"; then
    cat "$root/desktop.log" >&2
    echo "Extracted desktop app failed its QML launch check (status $result)" >&2
    exit 1
fi
