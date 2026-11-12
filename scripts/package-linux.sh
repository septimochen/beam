#!/bin/bash
set -euo pipefail

build=$1
cmake=$2
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
if [[ $(uname -sm) != 'Linux x86_64' ]] || ! grep -qx 'ID=ubuntu' /etc/os-release ||
    ! grep -qx 'VERSION_ID="26.04"' /etc/os-release; then
    echo 'Linux release archives require Ubuntu 26.04 x86-64.' >&2
    exit 1
fi
version=$("$cmake" -P "$source_dir/cmake/BeamVersion.cmake")
output="$build/release"
mkdir -p "$output"
stage=$(mktemp -d "$output/.stage.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT

for component in cli desktop; do
    DESTDIR="$stage/$component" "$cmake" --install "$build" --config Release \
        --prefix /usr/local --component "$component"
    name="beam-$component-v$version-linux-ubuntu26.04-x86_64.tar.gz"
    tar -czf "$stage/$name" --owner=0 --group=0 --numeric-owner \
        -C "$stage/$component/usr/local" bin share
done
mv -- "$stage"/*.tar.gz "$output/"
