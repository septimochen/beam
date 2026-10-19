#!/bin/bash
set -euo pipefail
source "$(dirname -- "$0")/macos-common.sh"
require_macos
applications="$HOME/Applications"
expected=org.beam.desktop
purge=false
confirmed=false
clear=false
staging=false
while (($#)); do
    case $1 in
        --user) applications="$HOME/Applications"; shift ;;
        --system) applications=/Applications; shift ;;
        --development) expected=org.beam.desktop.dev; shift ;;
        --applications-dir|--bundle-id)
            (($# >= 2)) || fail "Missing value for $1"
            if [[ $1 == --applications-dir ]]; then applications=$2; else expected=$2; fi
            shift 2 ;;
        --purge-data) purge=true; shift ;;
        --yes-delete-data) confirmed=true; shift ;;
        --clear-notifications) clear=true; shift ;;
        --staging) staging=true; shift ;;
        --help) echo 'Usage: uninstall-macos.sh [--user|--system|--applications-dir DIR] [--development|--bundle-id ID] [--clear-notifications] [--purge-data [--yes-delete-data]]'; exit 0 ;;
        *) fail "Unknown option: $1" ;;
    esac
done
validate_id "$expected"
validate_path "$applications"
# Resolve existing ancestors, but never follow a bundle symlink.
if [[ -d $applications ]]; then applications=$(cd -- "$applications" && pwd -P); fi
validate_path "$applications"
app_name=Beam.app
[[ $expected != org.beam.desktop.dev ]] || app_name='Beam Dev.app'
app="$applications/$app_name"
if [[ -e $app && ! -L $app ]]; then validate_bundle "$app" "$expected"; fi

data_paths=()
if $purge; then
    $staging && fail 'Data removal is unavailable for a staged uninstall.'
    [[ ${EUID} != 0 ]] || fail 'Run data removal as the owning user, without sudo.'
    validate_path "$HOME"
    state_root=${XDG_CONFIG_HOME:-$HOME/.config}
    validate_path "$state_root"
    data_paths=("$HOME/Library/Preferences/$expected.plist" "$HOME/Library/Caches/$expected"
        "$HOME/Library/Application Support/$expected" "$HOME/Library/Saved Application State/$expected.savedState"
        "$state_root/beam")
    # Check ancestors before any deletion. Unlink leaf symlinks, never follow them.
    for path in "${data_paths[@]}"; do
        parent=$(dirname -- "$path")
        while [[ $parent != / ]]; do
            [[ ! -L $parent ]] || fail "Data path has a symlink ancestor: $parent"
            parent=$(dirname -- "$parent")
        done
    done
    printf 'Remove Beam preferences/caches AND shared CLI/development pairing, identity and configuration:\n'
    printf '  %s\n' "${data_paths[@]}"
    if ! $confirmed; then
        [[ -t 0 ]] || fail 'Data deletion requires --yes-delete-data or an interactive confirmation.'
        read -r -p 'Type DELETE to confirm: ' answer
        [[ $answer == DELETE ]] || fail 'Data removal cancelled.'
    fi
fi
if ! $staging; then cleanup_build_apps; fi
if ! $staging && [[ ! -L $app ]]; then
    stop_app "$app" "$expected"
    if $clear && [[ -d $app && ! -L $app ]]; then
        # Run the bundled executable, retaining its notification identity. Older
        # releases without this command are left untouched and reported honestly.
        if codesign --verify --deep --strict "$app" &&
           "$app/Contents/MacOS/beam-desktop" --help | /usr/bin/grep -q -- --clear-notifications; then
            if ! "$app/Contents/MacOS/beam-desktop" --clear-notifications; then warn 'Notification cleanup failed'; fi
        else
            warn 'This installation does not support notification cleanup'
        fi
    elif $clear; then
        warn 'Bundle is absent; notification cleanup requires its executable'
    fi
    # For an absent bundle, only this explicitly selected installation path is used.
    if [[ ! -L $app ]]; then unregister_app "$app"; fi
fi
if [[ -L $app ]]; then rm -- "$app"; else rm -rf -- "$app"; fi
if $purge; then
    if [[ ! -L $HOME/Library/Preferences/$expected.plist ]]; then
        defaults delete "$expected" >/dev/null 2>&1 || true
    fi
    for path in "${data_paths[@]}"; do
        if [[ -L $path ]]; then rm -- "$path"; else rm -rf -- "$path"; fi
    done
    echo 'Removed the selected Beam preferences, caches, and shared identity/configuration.'
else
    echo 'User data retained.'
fi
echo "Uninstalled $app. Beam installs no launch agents, login items, or background helpers."
notification_note
