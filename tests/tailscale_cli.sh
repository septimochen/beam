#!/usr/bin/env bash
set -euo pipefail
beam=$1
root=$(mktemp -d)
pid=
trap 'if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$root"' EXIT
cat >"$root/fake tailscale" <<'SH'
#!/bin/sh
[ "$#" -eq 2 ] && [ "$1" = status ] && [ "$2" = --json ] || exit 8
printf '%s\n' '{"BackendState":"Running","Peer":{"key":{"DNSName":"laptop.test.ts.net.","Online":true,"TailscaleIPs":["100.64.0.20"]}}}'
SH
chmod 700 "$root/fake tailscale"
"$beam" devices --tailscale --tailscale-cli "$root/fake tailscale" --port 5270 --timeout 2 >"$root/output" 2>"$root/errors"
grep -q 'laptop.test.ts.net.' "$root/output"
grep -q '100.64.0.20:5270' "$root/output"
grep -q 'online' "$root/output"
grep -q 'untrusted' "$root/errors"
cat >"$root/fake tailscale" <<SH
#!/bin/sh
echo \$\$ >"$root/child"
exec sleep 30
SH
"$beam" devices --tailscale --tailscale-cli "$root/fake tailscale" --timeout 10 >"$root/output" 2>"$root/errors" &
pid=$!
for attempt in {1..500}; do
    if [[ -s "$root/child" ]]; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then cat "$root/errors"; exit 1; fi
    sleep 0.01
done
[[ -s "$root/child" ]]
kill -INT "$pid"
status=0
wait "$pid" || status=$?
pid=
[[ "$status" == 130 ]]
grep -q 'cancelled' "$root/errors"
if kill -0 "$(cat "$root/child")" 2>/dev/null; then echo 'Tailscale child survived cancellation'; exit 1; fi
echo 'Tailscale CLI output, port selection, Ctrl-C and child cleanup verified'
