#!/bin/bash
set -euo pipefail
source "$(dirname -- "$0")/macos-common.sh"
require_macos
build_dir="$beam_source/build/macos/release"
applications="$HOME/Applications"
expected=org.beam.desktop
config=Release
register=true
staging=false
while (($#)); do
    case $1 in
        --user) applications="$HOME/Applications"; shift ;;
        --system) applications=/Applications; shift ;;
        --development) expected=org.beam.desktop.dev; config=Debug; build_dir="$beam_source/build/macos/dev"; shift ;;
        --applications-dir|--build-dir|--bundle-id|--config)
            (($# >= 2)) || fail "Missing value for $1"
            case $1 in
                --applications-dir) applications=$2 ;;
                --build-dir) build_dir=$2 ;;
                --bundle-id) expected=$2 ;;
                --config) config=$2 ;;
            esac
            shift 2 ;;
        --no-register) register=false; shift ;;
        --staging) staging=true; register=false; shift ;;
        --help) echo 'Usage: install-macos.sh [--user|--system|--applications-dir DIR] [--development] [--build-dir DIR] [--config CONFIG] [--bundle-id ID] [--no-register]'; exit 0 ;;
        *) fail "Unknown option: $1" ;;
    esac
done
validate_id "$expected"
validate_path "$applications"
validate_path "$build_dir"
app_name=Beam.app
[[ $expected != org.beam.desktop.dev ]] || app_name='Beam Dev.app'
mkdir -p -- "$applications"
applications=$(cd -- "$applications" && pwd -P)
validate_path "$applications"
destination="$applications/$app_name"
[[ ! -L $destination ]] || fail "Refusing to replace a symlink: $destination"
if [[ -e $destination ]]; then validate_bundle "$destination" "$expected"; fi
# Finish dependency fixups/signing in a private sibling before touching an upgrade.
stage=$(mktemp -d "$applications/.beam-install.XXXXXXXX")
cleanup() {
    if [[ -d $stage/previous.app ]]; then
        warn "Previous installation retained for recovery: $stage/previous.app"
    else
        rm -rf -- "$stage"
    fi
}
trap cleanup EXIT
env -u DESTDIR "${CMAKE:-cmake}" --install "$build_dir" --config "$config" --prefix "$stage" --component desktop-staging
validate_bundle "$stage/Beam.app" "$expected"
codesign --verify --deep --strict "$stage/Beam.app"
if [[ -e $destination ]]; then
    if ! $staging; then stop_app "$destination" "$expected"; fi
    mv -- "$destination" "$stage/previous.app"
fi
if ! mv -- "$stage/Beam.app" "$destination"; then
    if [[ -d $stage/previous.app ]]; then mv -- "$stage/previous.app" "$destination"; fi
    fail 'Could not publish the new application; previous installation restored.'
fi
rm -rf -- "$stage/previous.app"
if $register; then
    if [[ ! -x $beam_lsregister ]] || ! "$beam_lsregister" -f "$destination"; then warn "Could not register $destination"; fi
fi
echo "Installed $destination ($expected)."
