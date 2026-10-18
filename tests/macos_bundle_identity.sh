#!/bin/bash
set -euo pipefail
source_dir=$1
build=$2
source "$source_dir/scripts/macos-common.sh"
root=$(mktemp -d "$build/.beam bundle identity.XXXXXXXX")
cleanup() {
    env -u BEAM_LSREGISTER "$source_dir/scripts/cleanup-macos.sh" --development --root "$root" >/dev/null 2>&1 || true
    rm -rf -- "$root"
}
trap cleanup EXIT
export CLANG_MODULE_CACHE_PATH="$root/module-cache"
configure_identity() {
    local build_type=$1 flags=$2 override=$3 expected=$4
    if ! cmake -S "$source_dir" -B "$root/configure" -G Ninja -DBEAM_ENABLE_DESKTOP=ON \
        -DBUILD_TESTING=OFF "-DCMAKE_BUILD_TYPE=$build_type" "-DCMAKE_CXX_FLAGS=$flags" \
        "-DBEAM_MACOS_BUNDLE_ID=$override" > "$root/configure.log" 2>&1; then
        cat "$root/configure.log" >&2
        return 1
    fi
    [[ $(plutil -extract CFBundleIdentifier raw -o - "$root/configure/beam-desktop.app/Contents/Info.plist") == "$expected" ]]
    unregister_app "$root/configure/beam-desktop.app" >/dev/null 2>&1
}
configure_identity Debug '' '' org.beam.desktop.dev
configure_identity Release '' '' org.beam.desktop
configure_identity RelWithDebInfo '' '' org.beam.desktop.dev
configure_identity Release -fsanitize=address '' org.beam.desktop.dev
configure_identity Release '' org.beam.desktop.experiment org.beam.desktop.experiment
if ! cmake -S "$source_dir" -B "$root/multi" -G 'Ninja Multi-Config' -DBEAM_ENABLE_DESKTOP=ON \
    -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release > "$root/multi.log" 2>&1; then
    cat "$root/multi.log" >&2
    exit 1
fi
for configuration in Debug Release; do
    plist="$root/multi/$configuration/beam-desktop.app/Contents/Info.plist"
    [[ $(plutil -extract CFBundleIdentifier raw -o - "$plist") == org.beam.desktop.dev ]]
done
if cmake -S "$source_dir" -B "$root/configure" -DBEAM_MACOS_BUNDLE_ID=org.other.app > "$root/invalid.log" 2>&1; then
    echo 'An unrelated bundle identifier was accepted' >&2
    exit 1
fi
echo 'Debug, Release, sanitizer, multi-config, and configurable bundle identities passed.'
