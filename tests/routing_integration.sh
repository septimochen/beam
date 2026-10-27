#!/usr/bin/env bash
set -euo pipefail
beam=$1
helper=$2
root=$(mktemp -d)
root=$(cd "$root" && pwd -P)
receiver_pid=
trap 'if [[ -n "$receiver_pid" ]]; then kill "$receiver_pid" 2>/dev/null || true; wait "$receiver_pid" 2>/dev/null || true; fi; rm -rf "$root"' EXIT
cd "$root"
mkdir output
port=$((35000 + $$ % 14000))
primary="127.0.0.1:$((port + 1))"
fallback="127.0.0.1:$port"
for device in alice bob charlie; do
    "$beam" identity init --state-dir "$root/$device" >"$device.identity"
    "$beam" identity export --state-dir "$root/$device" >"$device.pem"
done
fingerprint() { awk '/^Fingerprint: / {print $2}' "$1.identity"; }
for device in bob charlie; do
    "$beam" pair "$device" --state-dir "$root/alice" --cert "$device.pem" --fingerprint "$(fingerprint "$device")" --endpoint "$fallback"
    "$beam" pair alice --state-dir "$root/$device" --cert alice.pem --fingerprint "$(fingerprint alice)"
done
printf 'routed file\n' >sample.txt
# Updating either route preserves the other route and the paired certificate.
"$beam" peer bob --state-dir "$root/alice" --tailscale-endpoint 100.64.0.2:4269
"$beam" peer bob --state-dir "$root/alice" --endpoint "$fallback"
"$beam" peers --state-dir "$root/alice" >peers.log
grep -q '100.64.0.2:4269' peers.log
grep -q "$(fingerprint bob)" peers.log
cp "$root/alice/bob.peer" saved.peer
if "$beam" peer bob --state-dir "$root/alice" --tailscale-endpoint 192.168.1.2:4269 >bad.log 2>&1; then echo 'non-Tailscale route accepted'; exit 1; fi
cmp saved.peer "$root/alice/bob.peer"
"$beam" peer bob --state-dir "$root/alice" --clear-tailscale
"$beam" peers --state-dir "$root/alice" >peers.log
if grep -q '100.64.0.2:4269' peers.log; then echo 'route removal failed'; exit 1; fi
"$beam" peer bob --state-dir "$root/alice" --tailscale-endpoint '[fd7a:115c:a1e0::2]:4269'
start_receiver() {
    local receiver=${1:-bob}
    if [[ "$receiver" == hold-completion ]]; then
        "$helper" hold-completion "$root/bob" alice "$fallback" "$primary" sample.txt >receiver.log 2>&1 &
    else
        "$beam" receive --once --state-dir "$root/$receiver" --listen "$port" --output output --timeout 3 --no-discovery >received-text 2>receiver.log &
    fi
    receiver_pid=$!
    for attempt in {1..500}; do
        if grep -q '^Listening' receiver.log; then return; fi
        if ! kill -0 "$receiver_pid" 2>/dev/null; then cat receiver.log; echo 'receiver exited before readiness'; exit 1; fi
        sleep 0.01
    done
    cat receiver.log; echo 'receiver did not become ready'; exit 1
}
wait_receiver() {
    local status=0
    wait "$receiver_pid" || status=$?
    receiver_pid=
    return "$status"
}
run_helper() {
    "$helper" "$1" "$root/alice" bob "$2" "$3" sample.txt >sender.log 2>&1
}
# All payload kinds use the common pre-authentication fallback path.
for kind in file text url; do
    start_receiver
    if ! run_helper "fallback-$kind" "$primary" "$fallback"; then cat sender.log receiver.log; exit 1; fi
    if ! wait_receiver; then cat receiver.log; exit 1; fi
    case "$kind" in
        file) cmp sample.txt output/sample.txt ;;
        text) printf 'routed text' >expected; cmp expected received-text ;;
        url) printf 'https://example.com/' >expected; cmp expected received-text ;;
    esac
    rm -f output/*
done
# A direct route wins without touching the configured unavailable Tailscale route.
start_receiver
if ! "$beam" text 'direct preferred' bob --state-dir "$root/alice" --timeout 3 >sender.log 2>&1; then cat sender.log receiver.log; exit 1; fi
if ! wait_receiver; then cat receiver.log; exit 1; fi
[[ $(grep -c '^Trying ' sender.log) == 1 ]]
grep -q "Trying $fallback" sender.log
rm -f output/*
# Explicit endpoint and route selection retain the same TLS identity.
for option in direct override mapped; do
    start_receiver
    case "$option" in
        direct) route=(--route direct) ;;
        override) route=(--endpoint "$fallback") ;;
        mapped) route=(--endpoint "[::ffff:127.0.0.1]:$port") ;;
    esac
    if ! "$beam" send sample.txt bob --state-dir "$root/alice" --timeout 3 "${route[@]}" >sender.log 2>&1; then cat sender.log receiver.log; exit 1; fi
    if ! wait_receiver; then cat receiver.log; exit 1; fi
    cmp sample.txt output/sample.txt
    [[ $(grep -c '^Trying ' sender.log) == 1 ]]
    rm -f output/*
done
start_receiver charlie
if ! run_helper no-fallback-auth "$fallback" "$primary"; then cat sender.log receiver.log; exit 1; fi
if wait_receiver; then echo 'wrong identity accepted'; exit 1; fi
[[ -z "$(ls -A output)" ]]
printf keep >output/sample.txt
start_receiver
if ! run_helper no-fallback-reject "$fallback" "$primary"; then cat sender.log receiver.log; exit 1; fi
if wait_receiver; then echo 'receiver collision accepted'; exit 1; fi
[[ $(cat output/sample.txt) == keep ]]
rm output/sample.txt
start_receiver hold-completion
if ! run_helper no-fallback-late "$fallback" "$primary"; then cat sender.log receiver.log; exit 1; fi
if ! wait_receiver; then cat receiver.log; exit 1; fi
for mode in cancel-connect callback-error callback-wait; do
    if ! run_helper "$mode" "$primary" "$fallback"; then cat sender.log; exit 1; fi
done
if compgen -G 'output/.beam-*' >/dev/null; then echo 'routing failures leaked partials'; exit 1; fi
echo 'Phase 6: route storage/order, authenticated fallback, identity failures, rejection, cancellation and no replay verified'
