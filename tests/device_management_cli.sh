#!/usr/bin/env bash
set -euo pipefail
beam=$1
root=$(mktemp -d "${TMPDIR:-/tmp}/beam-devices-cli.XXXXXX")
root=$(cd "$root" && pwd -P)
trap 'rm -rf "$root"' EXIT
"$beam" identity init --state-dir "$root/local" > /dev/null
"$beam" identity init --state-dir "$root/phone" > /dev/null
"$beam" identity export --state-dir "$root/phone" > "$root/phone.pem"
fingerprint=$("$beam" identity show --state-dir "$root/phone" | awk '/Fingerprint:/ {print $2}')
"$beam" pair phone --cert "$root/phone.pem" --fingerprint "$fingerprint" --endpoint 127.0.0.1:4269 --state-dir "$root/local" > /dev/null
"$beam" peer phone --rename work-phone --endpoint '[::1]:5000' --tailscale-endpoint 100.64.0.2:5000 --state-dir "$root/local" > /dev/null
"$beam" peer work-phone --state-dir "$root/local" | grep -F '[::1]:5000' > /dev/null
"$beam" peers export "$root/devices.beam-backup" --state-dir "$root/local" > /dev/null
if "$beam" peers export "$root/devices.beam-backup" --state-dir "$root/local" 2>/dev/null; then exit 1; fi
"$beam" unpair work-phone --state-dir "$root/local" > /dev/null
"$beam" peers import "$root/devices.beam-backup" --state-dir "$root/local" | grep '1 to restore' > /dev/null
if "$beam" peer work-phone --state-dir "$root/local" 2>/dev/null; then exit 1; fi
"$beam" peers import "$root/devices.beam-backup" --approve --state-dir "$root/local" > /dev/null
"$beam" peer work-phone --clear-endpoint --clear-tailscale --state-dir "$root/local" > /dev/null
"$beam" peers import "$root/devices.beam-backup" --approve --state-dir "$root/local" | grep '0 to restore, 1 already paired' > /dev/null
if "$beam" peer work-phone --endpoint 127.0.0.1:5000 --clear-endpoint --state-dir "$root/local" 2>/dev/null; then exit 1; fi
if "$beam" peer work-phone --cert "$root/phone.pem" --state-dir "$root/local" 2>/dev/null; then exit 1; fi
printf 'device management CLI checks passed\n'
