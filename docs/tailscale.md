# Tailscale endpoints and routing

Phase 6 uses an already installed, connected Tailscale client on macOS/Linux.
Beam neither creates accounts nor changes VPN configuration. Transfers remain
Beam QUIC/TLS with explicitly paired certificates, SHA-256 verification, and
acknowledgement. Both devices still use `beam/3`; this release is Beam 0.6.0.

## Discover address hints

```sh
./build/beam devices --tailscale
./build/beam devices --tailscale --port 5270 --timeout 5
./build/beam devices --tailscale --tailscale-cli /path/to/tailscale
```

This runs the local CLI's read-only [`tailscale status --json`](https://tailscale.com/docs/reference/tailscale-cli)
command with separate arguments and no shell. `--tailscale-cli` supports a path
containing spaces, including a CLI inside a macOS application bundle. The default
executable is `tailscale` on PATH. A missing CLI, stopped client, malformed status,
or timeout produces an error. Ctrl-C returns exit status 130 and reaps the process.

The table contains online/offline tailnet peers and numeric `TailscaleIPs`.
It ignores the local device, physical WireGuard endpoints, and subnet routes.
The port defaults to 4269 or the supplied `--port`: discovery cannot establish
whether Beam is installed, listening on that port, or paired. Tailscale's `Online`
field describes connectivity to its control plane, not a Beam receiver check.
Names and addresses remain untrusted hints; discovery never updates pairing or routes.

## Store a route for a paired device

Complete reciprocal [pairing](pairing.md) first, then set addresses explicitly:

```sh
./build/beam peer macbook --endpoint 192.168.1.50:4269
./build/beam peer macbook --tailscale-endpoint 100.64.0.20:4269
./build/beam peers
```

IPv6 is also supported, for example
`--tailscale-endpoint '[fd7a:115c:a1e0::20]:4269'`. The Tailscale slot accepts
only numeric addresses within [100.64.0.0/10](https://tailscale.com/docs/concepts/tailscale-ip-addresses)
or [fd7a:115c:a1e0::/48](https://tailscale.com/docs/concepts/ipv6), including equivalent
IPv4-mapped IPv6 addresses. A range match never authorizes a device. DNS names are
not routing endpoints; the peer's stored TLS identity name remains unchanged.

Each update preserves the other address and the approved certificate.
`beam peer macbook --clear-tailscale` removes only the optional route;
`beam unpair macbook` removes the entire trusted-peer record. An existing primary
endpoint in a Tailscale range is treated as a Tailscale route, so older records
continue to work. Beam never maps discovered names to local pairing aliases.

The reader accepts legacy peer records. Address updates write a bounded versioned
record, `beam-peer-v2`, containing the primary endpoint, optional Tailscale endpoint,
and certificate. Older Beam binaries cannot read these updated records. The device
certificate, private key, fingerprint, and transfer wire format do not change.
Storage keeps its owner-only permissions, locking, and atomic publication.

## Transfer and endpoint preference

Start the receiver as usual, allowing its UDP port through the firewall and the
tailnet's grants/ACLs. Each receiver handles one transfer and then exits:

```sh
./build/beam receive --output received --no-discovery
./build/beam send photo.jpg macbook
./build/beam text "hello" macbook --route tailscale
./build/beam url "https://example.com/" macbook --route direct
```

`send`, `text`, and `url` share these choices:

| Option | Candidate endpoints |
| --- | --- |
| `--route auto` (default) | Saved non-Tailscale endpoint, then saved Tailscale endpoints |
| `--route direct` | Saved non-Tailscale endpoint only |
| `--route tailscale` | Saved Tailscale endpoints only |
| `--endpoint IP:PORT` | Exactly this address for this transfer |

Equivalent numeric addresses are normalized and deduplicated. A preference with
no saved address reports an error. `--endpoint` disables saved fallback and cannot
be combined with `--route`. Dedicated-CA transfers keep their single explicit
endpoint workflow. The CLI prints `Trying IP:PORT` to stderr; `--no-progress`
suppresses it along with progress. Sending never invokes the Tailscale CLI.

With multiple candidates, each connection setup wait gets at most three seconds,
or the smaller `--timeout`. A single candidate keeps the usual connection timeout.
Payload/verification waits retain their normal limits. A failed candidate is retried
only when the transport identifies an unreachable, refused, or timed-out connection
before authentication completes and before any application stream exists.

Certificate/handshake/protocol failures, receiver rejection, cancellation, and
callback exceptions stop immediately. Once a connection is authenticated, Beam
never falls back after an offer, payload bytes, or final acknowledgement failure.
This avoids replaying a transfer that may already have published verified output.
All candidates use the same immutable credentials and pinned receiver identity.

Tailscale chooses its own underlying direct or relayed path. Beam does not select
WireGuard endpoints, DERP relays, exit nodes, or subnet routers. LAN mDNS remains
an independent optional discovery source; neither source changes trust.

## Build and core API

Local CLI discovery adds the header-only `nlohmann_json` dependency (3.11+), rather
than implementing a JSON parser. Install `nlohmann-json` with Homebrew or
`nlohmann-json3-dev` on Debian/Ubuntu. It has no runtime library or network service.
`BEAM_ENABLE_TAILSCALE` defaults to the QUIC setting; disable it with
`-DBEAM_ENABLE_TAILSCALE=OFF` to remove the JSON and subprocess dependency.
Saved Tailscale endpoints and manually selected transfers still work in any QUIC build.

`routing.hpp` exposes numeric endpoint validation, address classification, and
pure `peer_endpoints` selection. `TransferOptions::fallback_endpoints` accepts up
to three extra candidates; `connect_timeout` optionally shortens setup waits.
`on_connecting` runs synchronously on the calling thread and can abort by throwing.
Only the internal transport can mark a connection failure as safe to retry.

`tailscale.hpp` exposes bounded status parsing and local discovery. Output is
limited to 1 MiB, nesting to 16 levels, peers to 512, and addresses to 16 per peer.
Duplicate JSON keys are rejected. Invalid entries, nonnumeric addresses, and
unsafe terminal names are omitted. Discovery has a total deadline of at most
60 seconds, checks cancellation between reads, and kills/reaps its process group
on failure. Stdin/stderr are discarded; status secrets and CLI stderr are never logged.

## Verification

`make routing-test` covers route ordering, normalization, storage compatibility,
JSON/subprocess bounds, CLI output and Ctrl-C, authenticated localhost fallback
for every payload kind, and refusal to retry identity/rejection/post-payload failures.
These deterministic tests run in `make check` and CI without a Tailscale account
or daemon. macOS Debug/Release, ASan/UBSan, optional builds, and live Bonjour checks
passed. [Linux CI](https://github.com/septimochen/beam/actions/runs/38135083076) passed
LLVM 23 formatting/static analysis, transfer tests, and live Avahi discovery.
This development machine has no Tailscale installation, so a live
tailnet transfer has not been verified. On a connected tailnet, select
`--route tailscale`, compare received bytes, and confirm both pinned identities.
