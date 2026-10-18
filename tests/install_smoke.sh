#!/bin/bash
set -euo pipefail
cmake_command=$1
build=$2
shift 2
# A failing assertion must not leave discoverable temporary app bundles.
cleanup() {
    if [[ $(uname -s) == Darwin ]]; then
        for root in "$build/.install smoke" "$build/.install smoke relocated"; do
            if [[ -d $root ]]; then
                env -u BEAM_LSREGISTER "$(dirname -- "$0")/../scripts/cleanup-macos.sh" --development --root "$root" >/dev/null 2>&1 || true
            fi
        done
    fi
    rm -rf -- "$build/.install smoke" "$build/.install smoke relocated"
}
trap cleanup EXIT
"$cmake_command" "-DBEAM_BUILD_DIR=$build" "$@"
