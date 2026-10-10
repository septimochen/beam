# LAN discovery

Phase 3 discovers listening macOS/Linux receivers using standard DNS-SD over mDNS.
The backend uses macOS Bonjour and Linux `libavahi-compat-libdnssd`; no discovery
server, account, or persistent device database is involved. Avahi must be running
on Linux. Build with `-DBEAM_ENABLE_DISCOVERY=OFF` to omit this dependency.

## CLI behavior

`beam devices` browses for three seconds and prints names, numeric endpoints,
hostnames, and interface indices. `--timeout SECONDS` accepts 1-60 seconds. Ctrl-C
returns 130; argument errors return 2; daemon failures return 1. An empty result
returns 0. Devices are grouped by service name and interface, so a receiver reachable
on multiple interfaces can appear more than once. Names are display labels and may
change when the daemon handles a collision.

`beam receive ... --name Laptop` advertises only after the QUIC port is bound and
the receive directory and credentials have passed the existing transfer checks.
Names must be 1-63 printable ASCII bytes without leading/trailing spaces; the
default is `Beam`. Registration gets three seconds to complete. A daemon failure
warns on stderr without preventing direct transfer. `--no-discovery` disables
advertisement and cannot be combined with `--name`. The advertisement belongs to
the single-use receive invocation and is removed on success, failure, or Ctrl-C;
it can remain visible while the one transfer is in progress. Force-killing a process
can leave cached records until the daemon withdraws them or they expire.

Copy a discovered endpoint into the existing `beam send IP:PORT FILE ...` command.
In dedicated-CA mode, all certificate flags and verification still apply. Set `--server-name` to the
independently provisioned certificate name if its SAN does not contain the endpoint
IP. Never treat the advertised name or hostname as verified identity. For a paired peer, use `beam peer NAME --endpoint IP:PORT` or
`beam send FILE NAME --endpoint IP:PORT`; the saved certificate remains authoritative.
See [pairing](pairing.md) for setup.

## Records and bounds

- Service type: `_beam._udp`, domain: `local.`; SRV port is the bound QUIC UDP port.
- TXT: `v=2`, indicating support for the existing `beam/2` transfer protocol. Unknown
  TXT keys are ignored; missing, duplicate, unsupported, or malformed versions are
  rejected. TXT data is limited to 512 bytes.
- No file names, paths, certificates, fingerprints, secrets, or transfer contents
  are advertised. The host/address information comes from the mDNS daemon.
- Browse at most 128 service/interface pairs with at most 16 addresses each. Invalid
  records, control characters, non-ASCII/overlong service names, non-local hostnames,
  zero ports, and unresolved services are omitted. Results beyond the bounds are
  ignored, so a busy or hostile LAN can yield an incomplete list.
- Query A and AAAA records asynchronously on the announcing interface; do not use
  blocking system hostname resolution. Deduplicate address additions and honor
  service/address removals. IPv6 link-local endpoints carry the numeric scope ID
  (`[fe80::1%12]:4269`). Unspecified, multicast, and limited-broadcast addresses are omitted.
- Browse uses one total deadline, not a renewed timeout per service. Stop tokens
  and cancellation predicates run on the application thread. Backend callback
  exceptions do not cross the C API. Resource ownership includes pending operations
  and all failure paths.

Discovery is unauthenticated. Any local machine can spoof a record or a name. It
does not add a CA, approve a peer, weaken TLS, or make a discovered service trusted.
Receivers authenticate using explicitly [paired certificates](pairing.md) or the
dedicated private CA described in [TLS setup](tls.md).
Advertising reveals that a Beam receiver is running, its label, host, and port to
other devices on the link. Use `--no-discovery` when that visibility is unwanted.

## Verification and troubleshooting

`make check` includes a pipe-backed daemon substitute that drives actual DNS-SD
callbacks and the event loop without multicast or a running daemon. Transfer tests
use `--no-discovery`, keeping localhost QUIC validation independent of discovery.
`make discovery-test` additionally runs real registration/browse/address/withdrawal
checks and CLI discovery followed by a verified QUIC transfer. Linux CI installs
Avahi development packages, starts the daemon, and runs this target.

If the list is empty, check that a receiver is running, both devices are on the
same multicast-capable link, and UDP 5353 is allowed. Guest Wi-Fi isolation, VPN
interface policies, containers, and restricted execution environments can prevent
mDNS traffic. Allow the receiver's QUIC UDP port separately. On Linux, check
`systemctl status avahi-daemon`. Direct IP transfers remain available when mDNS is
unavailable.

The backend follows the [Bonjour DNS-SD API](https://developer.apple.com/library/archive/documentation/Networking/Conceptual/dns_discovery_api/Introduction.html)
and the [Avahi compatibility API](https://github.com/avahi/avahi/blob/master/avahi-compat-libdns_sd/dns_sd.h).
