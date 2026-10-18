#!/bin/bash
# Explicit interactive OS check. Not in CTest: it registers and opens test apps.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
scripts="$source_dir/scripts"
[[ -d $HOME/Applications ]] || mkdir -p "$HOME/Applications"
root=$(mktemp -d "$HOME/Applications/Beam lifecycle test.XXXXXXXX")
cleanup() {
    "$scripts/uninstall-macos.sh" --applications-dir "$root" || return
    "$scripts/uninstall-macos.sh" --applications-dir "$root" --development || return
    rm -rf -- "$root"
}
trap cleanup EXIT
mkdir -p "$root/received"
running() { osascript -l JavaScript "$scripts/macos-stop.jxa" "$1" "$2" --check; }
launch() {
    open -n "$root/$1" --args --state-dir "$root/$2-state" --output "$root/received" --no-discovery
    for ((attempt=0; attempt<100; attempt++)); do
        if [[ $(running "$root/$1" "$3") == 1 ]]; then return; fi
        sleep 0.1
    done
    echo "Application failed to launch: $1" >&2
    exit 1
}
"$scripts/install-macos.sh" --development --applications-dir "$root"
launch 'Beam Dev.app' dev org.beam.desktop.dev
for cycle in 1 2; do
    "$scripts/install-macos.sh" --applications-dir "$root"
    launch Beam.app prod org.beam.desktop
    "$scripts/uninstall-macos.sh" --applications-dir "$root" --clear-notifications
    [[ ! -e $root/Beam.app && $(running "$root/Beam Dev.app" org.beam.desktop.dev) == 1 ]]
    echo "Live production install/launch/uninstall cycle $cycle passed; development app remains open."
done
"$scripts/uninstall-macos.sh" --applications-dir "$root" --development --clear-notifications
[[ ! -e $root/Beam\ Dev.app ]]
echo 'Live user-local lifecycle checks passed.'
