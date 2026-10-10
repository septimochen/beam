#!/usr/bin/env bash
set -euo pipefail
beam=$1
openssl=$2
root=$(mktemp -d)
root=$(cd "$root" && pwd -P)
receiver_pid=
trap 'if [[ -n "$receiver_pid" ]]; then kill "$receiver_pid" 2>/dev/null || true; wait "$receiver_pid" 2>/dev/null || true; fi; rm -rf "$root"' EXIT
cd "$root"
mkdir output
port=$((35000 + $$ % 15000))
for device in alice bob mallory charlie; do
    "$beam" identity init --state-dir "$root/$device" >"$device.identity"
    "$beam" identity export --state-dir "$root/$device" >"$device.pem"
    if grep -q 'PRIVATE KEY' "$device.pem"; then echo 'export leaked key'; exit 1; fi
done
fingerprint() { awk '/^Fingerprint: / {print $2}' "$1.identity"; }
for device in alice bob mallory charlie; do
    external_fingerprint=$("$openssl" x509 -in "$device.pem" -fingerprint -sha256 -noout | awk -F= '{print tolower($2)}' | tr -d ':')
    if [[ "$external_fingerprint" != "$(fingerprint "$device")" ]]; then echo 'identity fingerprint differs from OpenSSL'; exit 1; fi
done
pair() {
    "$beam" pair "$2" --state-dir "$root/$1" --cert "$2.pem" --fingerprint "$(fingerprint "$2")" --endpoint "127.0.0.1:$port"
}
pair alice bob
pair bob alice
pair bob charlie
pair charlie bob
pair mallory bob
# Restarting init must preserve identity; simultaneous init is serialized.
"$beam" identity init --state-dir "$root/alice" >restarted.identity
cmp alice.identity restarted.identity
for attempt in {1..5}; do
    "$beam" identity init --state-dir "$root/concurrent-$attempt" >first.identity &
    first_pid=$!
    "$beam" identity init --state-dir "$root/concurrent-$attempt" >second.identity &
    second_pid=$!
    first_status=0
    second_status=0
    wait "$first_pid" || first_status=$?
    wait "$second_pid" || second_status=$?
    if [[ "$first_status" != 0 || "$second_status" != 0 ]]; then echo 'concurrent initialization failed'; exit 1; fi
    cmp first.identity second.identity
done
"$beam" peers --state-dir "$root/bob" >peers.log
grep -q "$(fingerprint alice)" peers.log
grep -q "$(fingerprint charlie)" peers.log
start_receiver() {
    local credentials=(--state-dir "$root/${receiver:-bob}")
    if [[ -n "${receiver_cert:-}" ]]; then
        credentials=(--cert "$receiver_cert" --key "$root/bob/identity.pem" --ca alice.pem)
    fi
    "$beam" receive "${credentials[@]}" --listen "$port" --output output --timeout 3 --no-discovery >receiver.log 2>&1 &
    receiver_pid=$!
    for attempt in {1..500}; do
        if grep -q '^Listening' receiver.log; then return; fi
        if ! kill -0 "$receiver_pid" 2>/dev/null; then cat receiver.log; wait "$receiver_pid" || true; receiver_pid=; exit 1; fi
        sleep 0.01
    done
    cat receiver.log; echo 'paired receiver did not become ready'; exit 1
}
wait_receiver() {
    local status=0
    wait "$receiver_pid" || status=$?
    receiver_pid=
    return "$status"
}
send() {
    "$beam" send "$1" "${target:-bob}" --state-dir "$root/${sender:-alice}" --timeout 3 "${@:2}" >sender.log 2>&1
}
for sender in alice charlie; do
    printf 'paired transfer\000content\377\n' >"$sender.bin"
    start_receiver
    if ! send "$sender.bin"; then cat sender.log receiver.log; exit 1; fi
    if ! wait_receiver; then cat receiver.log; exit 1; fi
    cmp "$sender.bin" "output/$sender.bin"
done
unset sender
# Identity verification uses a persistent TLS name, independent of the IP address.
printf ipv6 >ipv6.bin
start_receiver
if ! send ipv6.bin --endpoint "[::1]:$port"; then cat sender.log receiver.log; exit 1; fi
if ! wait_receiver; then cat receiver.log; exit 1; fi
cmp ipv6.bin output/ipv6.bin
# Updating a saved address changes only routing, not pairing or identity.
"$beam" peer bob --state-dir "$root/alice" --endpoint "[::1]:$port"
printf updated >updated.bin
start_receiver
if ! send updated.bin; then cat sender.log receiver.log; exit 1; fi
if ! wait_receiver; then cat receiver.log; exit 1; fi
cmp updated.bin output/updated.bin
# Fingerprint mismatch cannot replace existing trust.
if "$beam" pair bob --state-dir "$root/alice" --cert mallory.pem --fingerprint "$(fingerprint bob)" >pair.log 2>&1; then echo 'fingerprint mismatch accepted'; exit 1; fi
"$beam" peers --state-dir "$root/alice" >peers.log
grep -q "$(fingerprint bob)" peers.log
# A sender who trusts the receiver but is not paired by it is rejected during TLS.
printf denied >denied.bin
sender=mallory
start_receiver
if send denied.bin; then echo 'unpaired sender accepted'; exit 1; fi
if wait_receiver; then echo 'receiver accepted unpaired sender'; exit 1; fi
unset sender
if [[ -e output/denied.bin ]]; then echo 'unauthorized file published'; exit 1; fi
# A receiver with a different identity cannot impersonate the saved alias.
pair alice charlie
receiver=charlie
start_receiver
if send denied.bin --endpoint "127.0.0.1:$port"; then echo 'wrong server identity accepted'; exit 1; fi
if wait_receiver; then echo 'wrong server accepted transfer'; exit 1; fi
unset receiver
# Reissuance with the same private key and TLS name is not automatic renewal:
# only the exact approved leaf certificate may authenticate the saved peer.
"$openssl" x509 -in bob.pem -signkey "$root/bob/identity.pem" -set_serial 2 -days 1 -out bob-reissued.pem
receiver_cert=bob-reissued.pem
start_receiver
if send denied.bin; then echo 'unapproved reissued certificate accepted'; exit 1; fi
if wait_receiver; then echo 'reissued server published transfer'; exit 1; fi
unset receiver_cert
# Removing one peer leaves other peers usable but immediately denies new sessions
# from the removed identity. Existing sessions retain their initial trust snapshot.
"$beam" unpair alice --state-dir "$root/bob"
start_receiver
if send denied.bin; then echo 'revoked sender accepted'; exit 1; fi
if wait_receiver; then echo 'revoked sender published transfer'; exit 1; fi
"$beam" unpair charlie --state-dir "$root/bob"
if "$beam" receive --state-dir "$root/bob" --listen "$port" --output output --timeout 1 --no-discovery >receiver.log 2>&1; then echo 'empty trust receiver accepted'; exit 1; fi
grep -q 'no trusted peers' receiver.log
if compgen -G 'output/.beam-*' >/dev/null; then echo 'pairing failures leaked partial files'; exit 1; fi
echo 'Phase 4: persistent identities, reciprocal pairing, name-based transfers, IPv6, pinning and revocation verified'
