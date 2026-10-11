# Beam protocol v3 (Phase 5)

ALPN: `beam/3`. QUIC/TLS authenticates both peers before any application stream.
Both peers must use Phase 5 builds. Protocol v1/v2 peers fail ALPN negotiation
before interpreting the new typed offer layout. Identity/pairing storage is unchanged.
One connection carries exactly one bidirectional control stream opened by the sender,
and one unidirectional payload stream opened after acceptance. All integers are unsigned
and big-endian. No HTTP/3 or bulk data multiplexing is used.

Control frames have a 6-byte header: version (u8, always 3), type (u8), payload length
(u32, at most 1024). A decoder rejects unsupported versions/types, truncated fields,
invalid lengths/text/error codes, zero IDs, and trailing bytes. Reads handle arbitrary
stream fragmentation; invalid headers and type-specific lengths fail before allocating
or waiting for their payload.

| Type | Value | Payload |
| --- | --- | --- |
| OFFER | 1 | id u64, content kind u8, size u64, SHA-256 32 bytes, filename length u16, filename bytes |
| ACCEPT | 2 | id u64 |
| REJECT | 3 | id u64, error code u16, reason length u16, printable ASCII reason bytes (1–256) |
| COMPLETE | 4 | id u64 |
| FAILED | 5 | id u64, error code u16, reason length u16, printable ASCII reason bytes (1–256) |
| ACKNOWLEDGED | 6 | id u64 |

Failures carry a bounded category. Received reason text is validated but not displayed
by the core; it reports the category through locally defined text. Local errors retain
filesystem/operation details, which are never sent to the peer.

| Error code | Meaning |
| --- | --- |
| 1 | Cancelled |
| 2 | Rejected |
| 3 | Destination exists |
| 4 | File I/O failed |
| 5 | Integrity verification failed |
| 6 | Payload size mismatch |
| 7 | Invalid protocol |
| 8 | Timed out |
| 9 | Connection failed |
| 10 | Input file changed |

Code 0 is reserved for ordinary connection closure and is invalid in failure frames.
Unknown nonzero application close codes become protocol errors. Stream aborts use the
same codes. After acceptance, a local failure closes the connection with its category
so a peer blocked sending or reading payload bytes can observe it immediately. Before
acceptance, the receiver sends REJECT when possible, then closes with the same code.
FAILED is supported by the decoder for peers that can send a control failure response;
the current receiver uses connection closure for failures after acceptance.
Cancellation uses application close code 1; it needs no control read on another thread.

The sender generates a random nonzero ID and hashes the input through a 64 KiB buffer.
It sends OFFER and waits for ACCEPT with the same ID. The payload stream begins with
id u64 followed by exactly the offered bytes and a QUIC FIN. The receiver rejects
extra/missing bytes, wrong IDs, and incorrect hashes. A filename must pass Beam's
portable basename validator; it cannot supply a path.

Content kinds are file=1 (`application/octet-stream`), text=2 (`text/plain`),
and URL=3 (`text/uri-list`). Unknown kinds fail closed. File offers require a safe
filename (1–255 bytes). Text/URL offers require a zero filename length and are
bounded to 65,536/8,192 payload bytes respectively; URL size cannot be zero.
Typed payloads stay on the independent payload stream, never in control metadata.
OFFER payload length is 51–306 bytes (51 for text/URL; 52–306 for files).

The receiver validates text/URL content after checking exact size, EOF and SHA-256.
Strict UTF-8 text permits tabs, CR and LF but excludes NUL, other C0/C1 controls
and DEL. URLs use the supported single absolute HTTP(S) ASCII/percent-encoded
syntax without credentials; see [content](content.md). Invalid content fails with
protocol error 7 before publication. Typed output names are generated locally as
`text-ID.txt` or `url-ID.txt`. Core file-only receivers reject typed offers with
code 2 before accepting payload bytes.

| Local state | Allowed next event |
| --- | --- |
| Sender waiting for acceptance | ACCEPT, REJECT, FAILED, or connection failure/cancellation |
| Sender streaming payload | Payload sends/FIN, or connection failure/cancellation |
| Sender waiting for verification | COMPLETE, REJECT, FAILED, or connection failure/cancellation |
| Receiver waiting for offer | OFFER, or connection failure/cancellation |
| Receiver receiving payload | Matching payload ID, exact bytes/FIN, or failure/cancellation |
| Receiver awaiting acknowledgement | ACKNOWLEDGED, REJECT, FAILED, or connection failure/cancellation |

Unexpected messages, IDs, stream directions, or trailing control bytes fail the
transfer. The receiver writes into a private, exclusively created temporary file
relative to a held output-directory descriptor. Only after valid size, digest, EOF,
content validation for text/URLs, and a cancellation check does it flush and link
the temporary file to its final name with no replacement. It then sends COMPLETE.
The sender sends ACKNOWLEDGED and
finishes the control send direction; the receiver finishes its direction after reading
the acknowledgement. The sender drains that final EOF before reporting success.

Progress callbacks report hashing, connecting/waiting, payload bytes, verification,
and complete on the calling thread. Payload progress reaching its total does not
mean verified success. The complete event follows the final control exchange.
Cancellation is checked between file buffers, before publication, and at most every
50 ms in network waits. A stop cannot interrupt a synchronous OS file operation or a
user callback; teardown still drains MsQuic callbacks before freeing owned storage.
Timeouts bound individual waits, not total transfer time, and spurious wakeups never
reset their deadline.

Cancellation/failure before publication removes the temporary file. A lost or cancelled
completion exchange can leave a fully verified file with a failed sender result; retries
never overwrite it. Cancellation after publication does not delete that file. No resume,
concurrency, arbitrary MIME types, Unicode filenames, or persistent connection reuse
is part of v3. This is an evolving protocol, not a promised stable ABI.
