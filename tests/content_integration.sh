#!/usr/bin/env bash
set -euo pipefail
beam=$1
helper=$2
root=$(mktemp -d)
root=$(cd "$root" && pwd -P)
receiver_pid=
sender_pid=
trap 'for pid in "$receiver_pid" "$sender_pid"; do if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; done; rm -rf "$root"' EXIT
cd "$root"
mkdir output
port=$((35000 + $$ % 15000))
endpoint="127.0.0.1:$port"
for device in alice bob; do
    "$beam" identity init --state-dir "$root/$device" >"$device.identity"
    "$beam" identity export --state-dir "$root/$device" >"$device.pem"
done
fingerprint() { awk '/^Fingerprint: / {print $2}' "$1.identity"; }
"$beam" pair bob --state-dir "$root/alice" --cert bob.pem --fingerprint "$(fingerprint bob)" --endpoint "$endpoint"
"$beam" pair alice --state-dir "$root/bob" --cert alice.pem --fingerprint "$(fingerprint alice)"
ready() {
    local pid=$1 log=$2 pattern=$3
    for attempt in {1..500}; do
        if grep -q "$pattern" "$log"; then return; fi
        if ! kill -0 "$pid" 2>/dev/null; then cat "$log"; echo 'process exited before readiness'; exit 1; fi
        sleep 0.01
    done
    cat "$log"; echo 'process did not become ready'; exit 1
}
start_receiver() {
    if [[ -n "${1:-}" ]]; then
        "$helper" "$1" "$root/bob" alice "$endpoint" output >receiver.log 2>&1 &
    else
        "$beam" receive --once --state-dir "$root/bob" --listen "$port" --output output --timeout 3 --no-discovery >received-text 2>receiver.log &
    fi
    receiver_pid=$!
    ready "$receiver_pid" receiver.log '^Listening'
}
wait_receiver() {
    local status=0
    wait "$receiver_pid" || status=$?
    receiver_pid=
    return "$status"
}
send() { "$beam" "$@" --state-dir "$root/alice" --timeout 3 >sender.log 2>&1; }
helper_send() { "$helper" "$1" "$root/alice" bob "$endpoint" output >sender.log 2>&1; }
check_output() {
    [[ -z "$(ls -A output)" ]] || { ls -la output; echo 'text was saved as a file'; exit 1; }
    { printf 'Received text: '; cat expected; printf '\n'; } >expected-display
    cmp expected-display received-text
    if grep -Fq 'PRIVATE-CONTENT' receiver.log sender.log; then echo 'content leaked to logs'; exit 1; fi
    if compgen -G 'output/.beam-*' >/dev/null; then echo 'partial file leaked'; exit 1; fi
}
reset_output() {
    if compgen -G 'output/.beam-*' >/dev/null; then echo 'partial file leaked'; exit 1; fi
    rm -f output/*
}
if [[ $(uname -s) == Darwin ]]; then
    # A failed Darwin UDP send can raise process-wide SIGPIPE. The CLI must
    # survive it and report the probe timeout through its usual error path.
    start_receiver hold-probe
    "$beam" check bob --state-dir "$root/alice" --timeout 1 >sender.log 2>&1 &
    sender_pid=$!
    ready "$receiver_pid" receiver.log '^Probe received'
    kill -PIPE "$sender_pid"
    status=0
    wait "$sender_pid" || status=$?
    sender_pid=
    [[ $status == 1 ]] || { cat sender.log; echo "probe exited with status $status after SIGPIPE"; exit 1; }
    grep -Eq 'beam \[(timed out|connection failed)\]' sender.log || { cat sender.log; echo 'probe lost its typed network error'; exit 1; }
    wait_receiver
    [[ -z "$(ls -A output)" ]]
fi
# Continuous reception survives idle waits, successive transfers, and rejected files.
"$beam" receive --state-dir "$root/bob" --listen "$port" --output output --timeout 1 --no-discovery >received-text 2>receiver.log &
receiver_pid=$!
ready "$receiver_pid" receiver.log '^Listening'
sleep 2
kill -0 "$receiver_pid" || { cat receiver.log; echo 'idle receiver exited'; exit 1; }
wait_for_listeners() {
    local count=$1
    for attempt in {1..500}; do
        if [[ $(grep -c '^Listening' receiver.log) -ge $count ]]; then return; fi
        if ! kill -0 "$receiver_pid" 2>/dev/null; then cat receiver.log; echo 'receiver exited'; exit 1; fi
        sleep 0.01
    done
    cat receiver.log; echo 'receiver did not restart'; exit 1
}
printf first >first.bin
printf second >second.bin
if ! send send first.bin bob; then cat sender.log receiver.log; exit 1; fi
wait_for_listeners 2
if ! send text 'continuous text' bob; then cat sender.log receiver.log; exit 1; fi
wait_for_listeners 3
if ! send text 'second message' bob; then cat sender.log receiver.log; exit 1; fi
wait_for_listeners 4
if send send first.bin bob; then echo 'receiver overwrote a file'; exit 1; fi
wait_for_listeners 5
if ! send send second.bin bob; then cat sender.log receiver.log; exit 1; fi
wait_for_listeners 6
cmp first.bin output/first.bin
cmp second.bin output/second.bin
printf 'Received text: continuous text\nReceived text: second message\n' >expected-display
cmp expected-display received-text
kill -INT "$receiver_pid"
status=0
wait_receiver || status=$?
[[ $status == 130 ]] || { cat receiver.log; echo 'continuous receiver did not cancel'; exit 1; }
reset_output
# UTF-8, newlines, shell-like text and empty payloads preserve exact bytes.
start_receiver
if ! send check bob; then cat sender.log receiver.log; exit 1; fi
[[ -z "$(ls -A output)" ]]
if ! send text 'after authenticated probe' bob; then cat sender.log receiver.log; exit 1; fi
wait_receiver
printf %s 'after authenticated probe' >expected
check_output text
for text in 'hello PRIVATE-CONTENT' $'你好 🌍\nline two\tend\r\n' '' 'https://example.com/ is text' '$(touch should-not-exist)'; do
    printf %s "$text" >expected
    start_receiver
    if ! send text "$text" bob; then cat sender.log receiver.log; exit 1; fi
    if ! wait_receiver; then cat receiver.log; exit 1; fi
    check_output text
    reset_output
done
[[ ! -e should-not-exist ]]
printf %s --literal >expected
start_receiver
if ! "$beam" text --state-dir "$root/alice" --timeout 3 -- --literal bob >sender.log 2>&1; then cat sender.log receiver.log; exit 1; fi
if ! wait_receiver; then cat receiver.log; exit 1; fi
check_output text
reset_output
# URLs use the paired TLS identity even with an explicit IPv6 endpoint.
url='https://example.com/%E4%BD%A0?q=PRIVATE-CONTENT#section'
printf %s "$url" >expected
start_receiver
if ! send text "$url" bob --endpoint "[::1]:$port" --no-progress; then cat sender.log receiver.log; exit 1; fi
if ! wait_receiver; then cat receiver.log; exit 1; fi
check_output url
reset_output
for mode in max-text url-text snapshot fragmented-text; do
    case "$mode" in
        max-text) printf '%65536s' '' | tr ' ' x >expected; kind=text ;;
        url-text) { printf 'https://example.com/'; printf '%65516s' '' | tr ' ' x; } >expected; kind=url ;;
        snapshot) printf 'immutable original' >expected; kind=text ;;
        fragmented-text) printf '你好' >expected; kind=text ;;
    esac
    start_receiver
    if ! helper_send "$mode"; then cat sender.log receiver.log; exit 1; fi
    if ! wait_receiver; then cat receiver.log; exit 1; fi
    check_output "$kind"
    reset_output
done
# Invalid metadata/content, digests and sizes never publish.
for mode in invalid-utf8 nul-text unknown-kind oversize-metadata obsolete checksum truncated oversized wrong-id; do
    start_receiver
    if ! helper_send "$mode"; then cat sender.log receiver.log; exit 1; fi
    if wait_receiver; then cat receiver.log; echo 'invalid content accepted'; exit 1; fi
    [[ -z "$(ls -A output)" ]] || { ls -la output; echo 'invalid content published'; exit 1; }
done
for mode in file-only cancel-receive cancel-verify callback-error; do
    start_receiver "$mode"
    if send text 'must not publish' bob; then echo 'failed receive reported success'; exit 1; fi
    if ! wait_receiver; then cat receiver.log; exit 1; fi
    [[ -z "$(ls -A output)" ]] || { ls -la output; echo 'failed receive published'; exit 1; }
done
if ! helper_send cancel-hash; then cat sender.log; exit 1; fi
start_receiver
if ! helper_send cancel-send; then cat sender.log receiver.log; exit 1; fi
if wait_receiver; then echo 'sender cancellation accepted'; exit 1; fi
[[ -z "$(ls -A output)" ]]
# A late Ctrl-C preserves already verified output.
start_receiver
helper_send hold-ack &
sender_pid=$!
ready "$sender_pid" sender.log '^Paused'
kill -INT "$receiver_pid"
status=0
wait_receiver || status=$?
[[ "$status" == 130 ]] || { cat receiver.log; echo 'incorrect cancellation exit'; exit 1; }
if ! wait "$sender_pid"; then cat sender.log; exit 1; fi
sender_pid=
[[ -z "$(ls -A output)" && ! -s received-text ]]
echo 'Text in memory: UTF-8, URL text, bounds, pairing, integrity, cancellation and no disk publication verified'
