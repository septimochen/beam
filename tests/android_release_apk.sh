#!/bin/bash
set -euo pipefail
[[ $# == 2 && ($2 == signed || $2 == unsigned) ]] || { echo 'Usage: android_release_apk.sh APK signed|unsigned' >&2; exit 2; }
apk=$1
mode=$2
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
root=$(mktemp -d "${TMPDIR:-/tmp}/beam-release-apk.XXXXXXXX")
trap 'rm -rf -- "$root"' EXIT
"${CMAKE:-cmake}" -DBEAM_ANDROID_ACTION=properties -P "$source_dir/cmake/AndroidTools.cmake" > "$root/toolchain"
sdk=$(sed -n 's/^sdk=//p' "$root/toolchain")
version=$(sed -n 's/^buildTools=//p' "$root/toolchain")
tools="$sdk/build-tools/$version"
"$tools/aapt2" dump badging "$apk" > "$root/manifest"
grep -Fq "package: name='org.beam.android'" "$root/manifest"
if grep -q '^application-debuggable' "$root/manifest"; then
    echo 'Release APK is debuggable' >&2
    exit 1
fi
unzip -Z1 "$apk" > "$root/entries"
grep -Fxq 'lib/arm64-v8a/libbeam-android.so' "$root/entries"
if grep '^lib/' "$root/entries" | grep -v '^lib/arm64-v8a/'; then
    echo 'Release APK contains an unsupported native ABI' >&2
    exit 1
fi
# Inspect packaged DEX, rather than trusting the R8 mapping or source keep rules.
: > "$root/native"
while IFS= read -r dex; do
    unzip -p "$apk" "$dex" > "$root/classes.dex"
    "$tools/dexdump" -d "$root/classes.dex" > "$root/dexdump"
    awk '
        /^Class #[0-9]/ { capture = 0 }
        /Class descriptor.*Lorg\/beam\/android\/Native;/ { capture = 1 }
        capture { print }
    ' "$root/dexdump" >> "$root/native"
done < <(grep -E '^classes([0-9]+)?\.dex$' "$root/entries")
awk '
    $1 == "name" && $2 == ":" { name = $3; gsub(/\047/, "", name) }
    $1 == "type" && $2 == ":" { type = $3; gsub(/\047/, "", type) }
    $1 == "access" && /NATIVE/ {
        if (name == "create" && type == "([BII)J") create = 1
        if (name == "destroy" && type == "(J)V") destroy = 1
        if (name == "call" && type == "(JI[[B)[[B") call = 1
    }
    END { exit !(create && destroy && call) }
' "$root/native" || { echo 'Release APK lost a required JNI entry point' >&2; exit 1; }
if "$tools/apksigner" verify "$apk" > "$root/signature" 2>&1; then
    [[ $mode == signed ]] || { echo 'Unsigned release was unexpectedly signed' >&2; exit 1; }
else
    [[ $mode == unsigned ]] || { cat "$root/signature" >&2; exit 1; }
fi
echo "Release APK validated: $mode, nondebuggable, ARM64, JNI entry points preserved."
