#!/bin/bash
set -euo pipefail
source "$(dirname -- "$0")/macos-common.sh"
require_macos
dry=false
development=false
remove_build_apps=false
root="$beam_source"
while (($#)); do
    case $1 in
        --dry-run) dry=true; shift ;;
        --development) development=true; shift ;;
        --remove-build-apps) remove_build_apps=true; development=true; shift ;;
        --root) (($# >= 2)) || fail 'Missing --root value'; root=$2; shift 2 ;;
        --help) echo 'Usage: cleanup-macos.sh [--dry-run] [--development|--remove-build-apps] [--root BUILD_ROOT]'; exit 0 ;;
        *) fail "Unknown option: $1" ;;
    esac
done
if ! $development; then dry=true; fi
validate_path "$root"
[[ -d $root ]] || fail "Build root does not exist: $root"
root=$(cd -- "$root" && pwd -P)
[[ $root != / && $root != "$HOME" && $root != /Applications && $root != "$HOME/Applications" ]] || fail 'Choose a dedicated build/repository root.'
records=$(mktemp -d "${TMPDIR:-/tmp}/beam-registrations.XXXXXXXX")
trap 'rm -rf -- "$records"' EXIT
# A never-registered .noindex bundle is deliberately invisible to the scanner;
# asking lsregister to unregister it reports a misleading "app not found" error.
if [[ -x $beam_lsregister ]] && "$beam_lsregister" -dump > "$records/initial-dump"; then
    awk -f "$beam_scripts/macos-registrations.awk" "$records/initial-dump" > "$records/registered"
fi
# Never scan another project, follow directory links, or change installed copies.
find "$root" -type d -name '*.app' -prune -print0 > "$records/bundles"
while IFS= read -r -d '' app; do
    id=$(bundle_id "$app") || continue
    case $id in org.beam.desktop|org.beam.desktop.dev) ;; *) continue ;; esac
    echo "Build bundle: $app ($id)"
    if $remove_build_apps; then
        if build_root=$(generated_build_root "$app"); then
            if [[ $app == "$build_root/apps.noindex/"* ]]; then
                echo "Retain unindexed build output: $app"
                if [[ -f $records/registered ]] && ! /usr/bin/grep -Fxq "$id"$'\t'"$app" "$records/registered"; then
                    continue
                fi
            else
                echo "Remove obsolete generated app: $app"
                if ! $dry; then
                    stop_app "$app" "$id"
                    unregister_app "$app"
                    rm -rf -- "$app"
                fi
                continue
            fi
        else
            echo "Retain app outside recognized CMake output: $app"
            continue
        fi
    fi
    if ! $dry; then unregister_app "$app"; fi
done < "$records/bundles"
if [[ -x $beam_lsregister ]] && "$beam_lsregister" -dump > "$records/dump"; then
    awk -f "$beam_scripts/macos-registrations.awk" "$records/dump" > "$records/parsed"
    while IFS=$'\t' read -r id app; do
        [[ $app == "$root/"* && $app == *.app ]] || continue
        # Reject ambiguous paths from the undocumented text dump.
        [[ $app != *$'\t'* && $app != */../* && $app != */./* && $app != *$'\r'* ]] || continue
        if has_symlink_ancestor "$app"; then continue; fi
        [[ ! -e $app && ! -L $app ]] || continue
        echo "Stale registration (missing bundle): $app ($id)"
        if ! $dry; then unregister_app "$app"; fi
    done < "$records/parsed"
else
    warn 'Cannot inspect stale Launch Services registrations on this system'
fi
if $dry; then
    echo 'Inspection only; no changes made.'
elif $remove_build_apps; then
    echo 'Obsolete generated apps cleaned; apps.noindex output and other files retained.'
else
    echo 'Build artifacts retained. Existing bundles may be rediscovered by macOS.'
fi
notification_note
