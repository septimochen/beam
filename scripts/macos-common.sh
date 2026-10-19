#!/bin/bash
# Sourced by Beam's macOS lifecycle commands. No changes at source time.
set -euo pipefail
beam_scripts=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
beam_source=$(cd -- "$beam_scripts/.." && pwd -P)
beam_lsregister=${BEAM_LSREGISTER:-/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister}
fail() { echo "Beam: $*" >&2; exit 1; }
warn() { echo "Beam: $* (best effort)" >&2; }
require_macos() { [[ $(uname -s) == Darwin ]] || fail 'This command requires macOS.'; }
validate_id() { [[ $1 =~ ^org\.beam\.desktop(\.[A-Za-z0-9-]+)*$ ]] || fail "Invalid Beam bundle identifier: $1"; }
validate_path() {
    [[ $1 == /* && $1 != *$'\n'* && $1 != *$'\r'* && $1 != *';'* ]] || fail "Expected an absolute path without newlines or semicolons: $1"
    [[ $1 != / && $1 != */../* && $1 != */.. && $1 != */./* && $1 != */. ]] || fail "Unsafe path: $1"
}
has_symlink_ancestor() {
    local parent
    parent=$(dirname -- "$1")
    while [[ $parent != / ]]; do
        if [[ -L $parent ]]; then return 0; fi
        parent=$(dirname -- "$parent")
    done
    return 1
}
bundle_id() { plutil -extract CFBundleIdentifier raw -o - "$1/Contents/Info.plist" 2>/dev/null; }
validate_bundle() {
    local app=$1 expected=$2
    [[ -d $app && ! -L $app && ! -L $app/Contents && ! -L $app/Contents/Info.plist && ! -L $app/Contents/MacOS ]] || fail "Unsafe application bundle: $app"
    [[ $(bundle_id "$app") == "$expected" ]] || fail "Bundle identity does not match $expected: $app"
    [[ $(plutil -extract CFBundleExecutable raw -o - "$app/Contents/Info.plist") == beam-desktop &&
       $(plutil -extract CFBundleName raw -o - "$app/Contents/Info.plist") == Beam &&
       -x $app/Contents/MacOS/beam-desktop && ! -L $app/Contents/MacOS/beam-desktop ]] || fail "Not a Beam application: $app"
}
stop_app() {
    # Public AppKit APIs; match both the identifier and the physical bundle path.
    osascript -l JavaScript "$beam_scripts/macos-stop.jxa" "$1" "$2" || fail "Could not stop $1; quit that copy of Beam and retry."
}
unregister_app() {
    if [[ -x $beam_lsregister ]]; then
        if "$beam_lsregister" -u "$1"; then
            echo "Launch Services unregistration requested: $1 (best effort)"
        else
            warn "Launch Services could not unregister $1"
        fi
    else
        warn 'Launch Services utility is unavailable'
    fi
}
generated_build_root() {
    # Recognize only CMake's known Beam output paths. An installed Beam.app
    # inside the repository, or an app from another project, is not disposable.
    local app=$1 parent relative cache
    [[ $app == */beam-desktop.app && ! -L $app && ! -L $app/Contents && ! -L $app/Contents/Info.plist ]] || return 1
    has_symlink_ancestor "$app" && return 1
    parent=$(dirname -- "$app")
    while [[ $parent == "$beam_source/"* ]]; do
        cache="$parent/CMakeCache.txt"
        if [[ -f $cache && ! -L $cache ]]; then
            /usr/bin/grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$beam_source" "$cache" || return 1
            /usr/bin/grep -Fxq 'CMAKE_PROJECT_NAME:STATIC=Beam' "$cache" || return 1
            /usr/bin/grep -Fxq 'BEAM_ENABLE_DESKTOP:BOOL=ON' "$cache" || return 1
            relative=${app#"$parent/"}
            case $relative in
                beam-desktop.app|src/desktop/beam-desktop.app|Debug/beam-desktop.app|Release/beam-desktop.app|RelWithDebInfo/beam-desktop.app|MinSizeRel/beam-desktop.app|apps.noindex/beam-desktop.app|apps.noindex/*/beam-desktop.app) ;;
                *) return 1 ;;
            esac
            [[ $(plutil -extract CFBundleExecutable raw -o - "$app/Contents/Info.plist" 2>/dev/null) == beam-desktop &&
               $(plutil -extract CFBundleName raw -o - "$app/Contents/Info.plist" 2>/dev/null) == Beam ]] || return 1
            printf '%s\n' "$parent"
            return 0
        fi
        parent=$(dirname -- "$parent")
    done
    return 1
}
cleanup_build_apps() {
    "$beam_scripts/cleanup-macos.sh" --remove-build-apps
}
notification_note() {
    echo 'macOS may retain Beam in System Settings > Notifications. Notification authorization and other apps are unchanged.'
    echo 'To remove a retained entry: Control-click Beam (or Beam Dev) in that list > Reset Notifications. This resets only that app; it must request permission again.'
}
