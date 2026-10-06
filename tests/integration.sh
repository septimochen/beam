#!/usr/bin/env bash
set -euo pipefail
beam=$1
openssl=$2
attacker=$3
root=$(mktemp -d)
receiver_pid=
trap 'if [[ -n "$receiver_pid" ]]; then kill "$receiver_pid" 2>/dev/null || true; wait "$receiver_pid" 2>/dev/null || true; fi; rm -rf "$root"' EXIT
cd "$root"
mkdir output source
"$openssl" req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days 1 -subj /CN=Beam-Test-CA >/dev/null 2>&1
for peer in server client; do
    "$openssl" req -newkey rsa:2048 -nodes -keyout "$peer.key" -out "$peer.csr" -subj "/CN=$peer" >/dev/null 2>&1
    printf 'basicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1\n' > extensions
    "$openssl" x509 -req -in "$peer.csr" -CA ca.pem -CAkey ca.key -CAcreateserial -out "$peer.pem" -days 1 -extfile extensions >/dev/null 2>&1
done
# Select a high port per process; every receiver is scoped and waited for.
port=$((30000 + $$ % 20000))
start_receiver() {
    SSL_CERT_FILE="${system_ca:-ca.pem}" "$beam" receive --listen "$port" --output output --cert "${server_cert:-server.pem}" --key "${server_key:-server.key}" --ca ca.pem --timeout 5 >receiver.log 2>&1 &
    receiver_pid=$!
    local attempt
    for attempt in {1..200}; do
        if grep -q '^Listening' receiver.log; then return; fi
        if ! kill -0 "$receiver_pid" 2>/dev/null; then cat receiver.log; wait_receiver || true; exit 1; fi
        sleep 0.01
    done
    echo 'receiver did not become ready'; cat receiver.log; exit 1
}
wait_receiver() {
    local status=0
    wait "$receiver_pid" || status=$?
    receiver_pid=
    return "$status"
}
send() {
    SSL_CERT_FILE="${system_ca:-ca.pem}" "$beam" send "${destination:-127.0.0.1}:$port" "$1" --cert client.pem --key client.key --ca ca.pem --timeout 5 "${@:2}" >sender.log 2>&1
}
for file in empty.bin small.bin large.bin; do
    case "$file" in
        empty.bin) : > "source/$file" ;;
        small.bin) printf 'Beam binary\000content\377\n' > "source/$file" ;;
        large.bin) dd if=/dev/urandom of="source/$file" bs=65536 count=48 2>/dev/null ;;
    esac
    start_receiver
    if ! send "source/$file"; then cat sender.log receiver.log; exit 1; fi
    if ! wait_receiver; then cat receiver.log; exit 1; fi
    cmp "source/$file" "output/$file"
done
# Exercise IPv6 numeric endpoint parsing, TLS SAN verification, and UDP connectivity.
cp source/small.bin source/ipv6.bin
destination='[::1]'
start_receiver
if ! send source/ipv6.bin; then cat sender.log receiver.log; exit 1; fi
if ! wait_receiver; then cat receiver.log; exit 1; fi
cmp source/ipv6.bin output/ipv6.bin
unset destination
# Existing destination: both processes fail, original bytes remain intact.
start_receiver
if send source/small.bin; then echo 'overwrite accepted'; exit 1; fi
if wait_receiver; then echo 'receiver accepted overwrite'; exit 1; fi
cmp source/small.bin output/small.bin
# Use a new basename so TLS tests cannot pass through an unrelated overwrite rejection.
cp source/small.bin source/tls.bin
# Server name mismatch and untrusted client certificate fail during TLS.
start_receiver
if send source/tls.bin --server-name wrong.invalid; then echo 'wrong TLS name accepted'; exit 1; fi
if wait_receiver; then echo 'receiver accepted failed TLS'; exit 1; fi
"$openssl" req -x509 -newkey rsa:2048 -nodes -keyout stranger.key -out stranger.pem -days 1 -subj /CN=Stranger >/dev/null 2>&1
system_ca=stranger.pem
start_receiver
if "$beam" send "127.0.0.1:$port" source/tls.bin --cert stranger.pem --key stranger.key --ca ca.pem --timeout 5 >sender.log 2>&1; then echo 'untrusted client accepted'; exit 1; fi
if wait_receiver; then echo 'receiver accepted untrusted client'; exit 1; fi
# A root in the default TLS store must not authorize a server outside --ca.
"$openssl" req -newkey rsa:2048 -nodes -keyout stranger-server.key -out stranger-server.csr -subj /CN=localhost >/dev/null 2>&1
"$openssl" x509 -req -in stranger-server.csr -CA stranger.pem -CAkey stranger.key -CAcreateserial -out stranger-server.pem -days 1 -extfile extensions >/dev/null 2>&1
server_cert=stranger-server.pem
server_key=stranger-server.key
start_receiver
if send source/tls.bin; then echo 'default-root server accepted outside explicit CA'; exit 1; fi
if wait_receiver; then echo 'receiver accepted server trust failure'; exit 1; fi
unset system_ca server_cert server_key
if [[ -e output/tls.bin ]]; then echo 'unauthorized TLS file published'; exit 1; fi
# Invalid numeric address must fail even when credentials are otherwise valid.
if "$beam" send "invalid:$port" source/small.bin --cert client.pem --key client.key --ca ca.pem --timeout 1 >sender.log 2>&1; then exit 1; fi
for mode in checksum truncated oversized wrong-id interrupted invalid-name invalid-version wrong-state wrong-direction; do
    start_receiver
    "$attacker" "$mode" "127.0.0.1:$port" client.pem client.key ca.pem unused >attacker.log 2>&1 || { cat attacker.log receiver.log; exit 1; }
    if wait_receiver; then echo "receiver accepted $mode"; exit 1; fi
    case "$mode" in
        checksum) expected='SHA-256 integrity verification failed' ;;
        truncated) expected='payload smaller than offered size' ;;
        oversized) expected='payload exceeds offered size' ;;
        wrong-id) expected='payload transfer id mismatch' ;;
        interrupted) expected='QUIC stream interrupted' ;;
        invalid-name) expected='unsafe offered filename' ;;
        invalid-version) expected='unsupported protocol version' ;;
        wrong-state) expected='expected transfer offer' ;;
        wrong-direction) expected='unexpected QUIC stream direction' ;;
    esac
    if ! grep -q "$expected" receiver.log; then cat receiver.log attacker.log; exit 1; fi
    if [[ -e output/attack.bin || -e evil ]]; then echo "published invalid $mode transfer"; exit 1; fi
    if compgen -G 'output/.beam-*' >/dev/null; then echo "temporary file leaked for $mode"; exit 1; fi
done
# A listener with no sender expires instead of hanging indefinitely.
"$beam" receive --listen "$port" --output output --cert server.pem --key server.key --ca ca.pem --timeout 1 >receiver.log 2>&1 && exit 1
if "$beam" receive --listen "$port" --output missing-directory --cert server.pem --key server.key --ca ca.pem --timeout 1 >receiver.log 2>&1; then exit 1; fi
grep -q 'open receive directory' receiver.log
echo 'QUIC integration: binary/empty/large success; overwrite, TLS, malicious and interrupted transfers rejected'
