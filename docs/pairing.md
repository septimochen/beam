# Device identity and pairing

Each macOS/Linux device keeps one self-signed certificate and private key. Pairing
stores a peer's exact certificate after you verify its full SHA-256 fingerprint
through an independent channel. Transfers authenticate both devices using the
existing QUIC/TLS connection. There is no network pairing protocol or trust on
first use.

## Initialize and exchange public certificates

On each device:

```sh
./build/beam identity init
./build/beam identity show
./build/beam identity export > my-device.pem
```

`init` is safe to repeat: it keeps the existing key, certificate, and fingerprint.
`show` prints the persistent TLS identity name and 64-character lowercase
fingerprint. `export` writes only the public certificate to stdout. Exchange
these exported certificates; never copy the private `identity.pem` from the state
store to another device.

Compare each fingerprint with `identity show` on the original device, in person
or through an independently authenticated channel. Do not copy a fingerprint
from the same untrusted message or discovery record that supplied a certificate.
A matching fingerprint proves which certificate you approved; TLS proves the
remote device possesses its private key.

## Pair both ways

For a sender called `desktop` and receiver called `laptop`, run on the desktop:

```sh
./build/beam pair laptop --cert laptop.pem --fingerprint VERIFIED_LAPTOP_SHA256 \
  --endpoint 192.168.1.50:4269
```

Run on the laptop:

```sh
./build/beam pair desktop --cert desktop.pem --fingerprint VERIFIED_DESKTOP_SHA256
```

Replace the fingerprint placeholders with the full verified values. An endpoint
is optional when adding a peer; sending requires either a saved endpoint or a
one-time `--endpoint` override. Aliases are local to each device and contain 1-63
lowercase letters, digits, `-`, or `_`, starting with a letter. They are unrelated
to the device's TLS identity or its advertised LAN name.

Pairing rejects invalid or expired certificates, private-key imports, fingerprint
mismatches, self-pairing, duplicate aliases, and duplicate identities. Replacing a
peer requires explicit removal first. Pairing on one device does not authorize
it on the other: both devices must approve each other's certificates.

## Transfer and manage peers

On the receiver:

```sh
mkdir -p received
./build/beam receive --output received
```

On the sender:

```sh
./build/beam send photo.jpg laptop
./build/beam peers
```

The default receive port is 4269; use `--listen PORT` to change it. Each invocation
accepts one authenticated connection and one file, then exits. A sender trusts
only the selected peer; a receiver accepts any currently paired peer. Empty trust
sets fail before opening a listener. The receiver still verifies size/SHA-256 and
never overwrites an existing file.

If the peer's address changes, keep its identity and update only routing:

```sh
./build/beam devices
./build/beam peer laptop --endpoint 192.168.1.60:4269
# Or use an address for just one transfer:
./build/beam send photo.jpg laptop --endpoint '[fe80::1%12]:4269'
```

Copy a numeric endpoint from discovery or supply one directly. Discovery cannot
create, change, or approve trust. The certificate's persistent DNS identity is
checked instead of the current IP address, and its exact fingerprint must match
the paired certificate. The CLI does not resolve aliases using advertised names.

To revoke a peer:

```sh
./build/beam unpair laptop
```

Removal denies new transfers when credentials are next loaded. A running transfer
or listener retains its original trust snapshot; stop and restart it to apply
revocation immediately. To replace a device, remove its old identity on each peer,
verify the replacement fingerprint independently, and pair again.

## Storage and recovery

The default store is `$XDG_CONFIG_HOME/beam` when that environment variable is
set, otherwise `$HOME/.config/beam` on both macOS and Linux. Use `--state-dir DIR`
on identity, pairing, peer-management, send, and receive commands for a separate
store. State directories must be owned by the current user with mode 0700; state
files must have mode 0600. The directory path must not contain symlinks. When
using a system temporary directory with symlinked ancestors, use its resolved
path (for example `/private/tmp` on macOS).

`identity.pem` contains the private key and certificate together so initialization
can publish them atomically. Each `NAME.peer` holds an optional numeric endpoint
and the approved public certificate. Directory-relative operations reject
symlink/hardlink records; a process lock serializes initialization and mutations.
Writes use exclusive temporary files, flush them, publish atomically, and flush
the directory. A crash may leave a `.pending-*` file; these never become identities
or peers and may be removed while Beam is stopped. At most 128 peers are stored;
individual input/state files are limited to 16 KiB.

`init` never silently replaces an expired or corrupt identity. If the identity
is missing while peer records remain, it refuses to generate a replacement.
Certificates are generated with an OpenSSL P-256 key, a random persistent DNS
name, SHA-256 signatures, and ten-year validity. There is no automatic certificate
renewal in this milestone. Back up the private store securely. Losing or replacing
it changes device identity and requires explicit re-pairing; an expired identity
also needs replacement. To start over, stop Beam and move the old store aside,
then run `identity init` and re-pair with a newly verified fingerprint.

## Compatibility

Paired transfers retain the `beam/2` wire protocol and all transfer cancellation,
progress, and cleanup behavior. Both peers need Phase 4 identity support for this
pairing workflow. Existing dedicated-CA transfers remain available with explicit
`--cert`, `--key`, and `--ca` flags; see [legacy TLS setup](tls.md#dedicated-ca-transfers).
Manual credentials cannot be mixed with paired-state options. There is no insecure
mode, and paired transfers never fall back to CA or system-root authorization.
