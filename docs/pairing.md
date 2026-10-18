# Device identity and pairing

Each macOS/Linux/Android device keeps one self-signed certificate and private key.
Use **Add device** for authenticated LAN pairing, or the advanced manual workflow
below. Transfers continue to authenticate both devices through the existing
QUIC/TLS connection and exact full SHA-256 leaf certificate pins.

## Add a device on the LAN

- **Phone and computer:** computer chooses **Add device → Show QR code**;
  Android chooses **Add device → Scan QR code**. Review the names and approve
  on both screens. Camera permission is requested only for scanning; denied or
  missing cameras leave invitation paste and nearby PIN pairing available.
- **Nearby devices:** open **Add device** on both, select the nearby device,
  and enter the six-digit PIN shown on its screen. Approve on both devices.
- **Blocked discovery:** use **Copy invitation** on the host and paste it on
  the other device. Direct LAN connectivity must still be available; guest Wi-Fi
  isolation can prevent all three flows.

The invitation expires after three minutes and is consumed by one authenticated
exchange. Cancel, closing the screen, or leaving the Android screen invalidates
it. Android recreation opens a new window; old screenshots cannot resume it.
A successful exchange saves both full certificates and observed transfer routes.
Aliases are suggested locally and can be edited before approval. A display name
or discovery record alone never authorizes a peer.

CLI equivalents:

```sh
beam pairing host --name laptop --qr
beam pairing nearby
beam pairing join --name phone
# Paste the invitation on stdin, then type yes (or an alias) on both devices.
beam pairing join --endpoint 192.168.1.20:PORT
# Enter the six-digit PIN on stdin; PORT comes from pairing discovery.
```

`beam pairing forget` explicitly clears an incomplete journal. `--qr` renders
a terminal QR code for scanning. `--state-dir DIR` selects an independent store; `--port PORT` specifies the
normal transfer listener's port (default 4269). The temporary pairing TCP port
is allocated by the OS. The host's stdout deliberately contains its invitation
and PIN; do not save or publish that output. Diagnostics omit secrets. These
commands work without a GUI; invitations/PINs need not enter shell history.

### Authentication and completion

Pairing has two distinct connections, both implemented by Mbed TLS 4.2.0:

1. EC J-PAKE authenticates a temporary encrypted TLS 1.2 channel using the
   256-bit QR/paste secret or the separate six-digit nearby PIN. Only the
   library's EC-JPAKE cipher suite is enabled; no password is sent in plaintext.
2. The devices exchange and validate their public certificates, then open a
   fresh TLS 1.3 connection with mandatory mutual certificate authentication,
   exact leaf pins, and the expected Beam TLS identity names. This proves each
   private key belongs to the exchanged certificate. QR/paste also checks the
   full host fingerprint carried by the invitation.
3. Each device approves its peer. Both approvals and preparation acknowledgements
   precede publishing any trusted-peer record. Storage and completion receipts
   are exchanged before reporting success.

The EC-JPAKE TLS profile is Mbed TLS's implementation-specific pairing profile;
this is a Beam-to-Beam protocol. It does not alter the transfer TLS configuration.
Session resumption and renegotiation are disabled. At most one exchange is active,
with five failed authentications per window and at least one second between
failed attempts. Discovery carries `_beam-pair._tcp` and `v=1`, never secrets.

### Interrupted pairing

`pairing.journal` is a bounded, locked, owner-only, atomically published record.
A prepared record is excluded from transfer trust. An interrupted final exchange
can leave one side already committed; two-device storage cannot be atomic.
**Incomplete** means the final receipts were not all observed, and is never
reported as success. Repeat pairing with the same device and alias: exact
certificate matching makes the commit idempotent. Different certificates or
conflicting aliases require explicit removal. **Forget pending approval** removes
only the journal; any committed peer remains visible and requires explicit unpair.
Private keys always stay on their original devices.

## Advanced manual pairing

Phase 6 adds an optional saved Tailscale endpoint beside the primary endpoint.
Route updates preserve the approved certificate and use versioned peer records;
the reader also accepts legacy records, while older Beam binaries cannot read an
updated record. See [Tailscale routing](tailscale.md) for configuration and fallback.

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
peer requires either explicit removal or the verified certificate replacement described below. Pairing on one device does not authorize
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
accepts one authenticated connection and one file or text, then exits. A sender trusts
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

In macOS, Linux, or Android, select the device in the paired-device picker and
choose **Unpair device**. Confirm the named device to remove it. Cancel leaves
trust and transfers unchanged. The app stops and joins active transfers and
connectivity checks before removal, then refreshes the picker and restarts
reception for the remaining devices. Removing the last peer stops reception.
Android preserves an explicit receiving Stop across this change.

Unpairing removes local trust and any matching interrupted-pairing journal; it
keeps your identity, other paired devices, settings, and received files. The other
device keeps its own record until you unpair there too. Reconnecting requires
pairing again.

CLI removal or certificate edits deny new transfers when credentials are next loaded.
A separately running CLI transfer or listener retains its original trust snapshot;
stop and restart those processes to apply changes immediately. Native apps cancel
and join workers before edits, then reload trust and resume reception. Android
keeps an explicit receiving Stop in effect.

## Device management

Open **Devices** in macOS, or **Manage devices** in Linux/Android. Search by device
name or address. Each card shows saved direct/Tailscale routes and a separate
**Check connection** action. Checks use authenticated QUIC/TLS, transfer no content,
and show the result of that check; refreshing the saved list does not imply a
connectivity check. **Add device** keeps the QR, nearby PIN, and invitation flows.
**My identity** exposes only your public certificate and full SHA-256 fingerprint.

**Edit** changes the local name and IP/port together. Names retain the CLI alias
rules (lowercase, 1–63 characters, starting with a letter); the remote device's
name is unaffected. Use `IP:PORT` or `[IPv6]:PORT`. Leave a route blank to remove
it. Tailscale routes must use an address in the supported Tailscale ranges.
Renaming preserves the certificate and does not require pairing again.

```sh
beam peer laptop                              # inspect saved details
beam peer laptop --rename work-mac --endpoint 192.168.1.60:4269
beam peer work-mac --tailscale-endpoint 100.64.0.2:4269
beam peer work-mac --clear-endpoint --clear-tailscale
beam check work-mac                           # authenticated, bounded check
beam peers                                    # refresh the saved list
beam devices                                  # discover untrusted LAN hints
```

For a renewed/replaced remote identity, enable **Replace certificate** in Edit,
choose/paste its exported public PEM, and enter the full independently verified
64-character lowercase SHA-256 fingerprint. **Save & trust certificate** (macOS)
or saving with replacement enabled (Linux/Android) explicitly changes local trust.
Invalid, expired, self, duplicate, mismatched, private-key, or stale replacements
leave records unchanged. Verify and approve this device on the remote device too
if its trust was reset. Do not import a local private key as a device certificate.

```sh
beam peer work-mac --cert replacement-public.pem --fingerprint VERIFIED_NEW_SHA256
```

### Back up and restore paired devices

**Export backup** writes a portable `.beam-backup` file containing local aliases,
direct/Tailscale endpoints, public certificates, their exact fingerprints, and the
exporting device's fingerprint. It contains **no private key**, received content,
settings, or pending pairing approvals. The CLI/desktop exporter creates a new
owner-only file and refuses to overwrite existing paths. Android uses the system
Create document picker; the selected provider controls its destination/access.

**Import backup** first validates the entire file and shows names, routes, full
fingerprints, and counts. Review it, then choose **Restore devices**. Restore only
a backup whose source you trust: this readable format is not encrypted or signed,
and approval adds the included certificates to local trust. At most 128 devices
and 2 MiB per backup are accepted; individual records retain the 16 KiB limit.
Unknown versions, trailing/truncated data, duplicate names/identities, malformed
routes, invalid certificates, and conflicts fail before changing trust. No entries
are silently replaced. Existing name/certificate matches keep their current
settings, so repeated restoration is safe. Rename or unpair conflicting records
explicitly before importing again.

```sh
beam peers export beam-devices.beam-backup
beam peers import beam-devices.beam-backup            # preview only
beam peers import beam-devices.beam-backup --approve  # restore after review
```

All these commands support `--state-dir DIR`. On a fresh CLI installation, run
`beam identity init` before importing. Native device management initializes an
identity on first use and never replaces an existing or damaged one.
A backup restores peer records on
the original device while its persistent identity is intact. Reinstalling Android,
losing `identity.pem`, or moving the backup to a new identity requires approving
that new identity on the other devices before transfers can work. The import
screen warns when the source fingerprint differs. A device-list backup does not
recover a lost private identity or bypass reciprocal pairing.

Edits and imports use one locked, durable desired-state transaction. Every store
opener completes an interrupted transaction before exposing peers/credentials;
no partial set becomes visible to a running Beam client. A failed disk operation
can leave `devices.transaction`; preserve it and retry after fixing storage.
Finish or explicitly forget interrupted pairing before editing/restoring devices.

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
individual identity/peer/pairing files are limited to 16 KiB. The bounded
`devices.transaction` may be up to 2 MiB.

`init` never silently replaces an expired or corrupt identity. If the identity
is missing while peer records remain, it refuses to generate a replacement.
Certificates are generated with an OpenSSL P-256 key, a random persistent DNS
name, SHA-256 signatures, and ten-year validity. There is no automatic certificate
renewal in this milestone. Back up the private store securely. Losing or replacing
it changes device identity and requires explicit re-pairing; an expired identity
also needs replacement. To start over, stop Beam and move the old store aside,
then run `identity init` and re-pair with a newly verified fingerprint.

## Compatibility

Paired transfers retain the `beam/4` wire protocol and all transfer cancellation,
progress, and cleanup behavior. Both peers need Phase 4 identity support for this
pairing workflow. Existing dedicated-CA transfers remain available with explicit
`--cert`, `--key`, and `--ca` flags; see [legacy TLS setup](tls.md#dedicated-ca-transfers).
Manual credentials cannot be mixed with paired-state options. There is no insecure
mode, and paired transfers never fall back to CA or system-root authorization.
