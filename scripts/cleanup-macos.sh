#!/bin/bash
set -euo pipefail
source "$(dirname -- "$0")/macos-common.sh"
require_macos
dry=false
development=false
root="$beam_source"
while (($#)); do
    case $1 in
        --dry-run) dry=true; shift ;;
        --development) development=true; shift ;;
        --root) (($# >= 2)) || fail 'Missing --root value'; root=$2; shift 2 ;;
        --help) echo 'Usage: cleanup-macos.sh [--dry-run|--development] [--root BUILD_ROOT]'; exit 0 ;;
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
# Never scan another project, follow directory links, or change installed copies.
find "$root" -type d -name '*.app' -prune -print0 > "$records/bundles"
while IFS= read -r -d '' app; do
    id=$(bundle_id "$app") || continue
    case $id in org.beam.desktop|org.beam.desktop.dev) ;; *) continue ;; esac
    echo "Build bundle: $app ($id)"
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
echo 'Build artifacts retained. Existing bundles may be rediscovered by macOS.'
notification_note
