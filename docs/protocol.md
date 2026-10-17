# Beam protocol v4

ALPN: `beam/4`. QUIC/TLS authenticates both peers before any application stream.
Both peers must use Beam 0.9 or later. Protocol v1/v2/v3 peers fail ALPN negotiation
before interpreting the new typed offer layout. Identity/pairing storage is unchanged.
One connection carries exactly one bidirectional control stream opened by the sender,
and one unidirectional payload stream opened after acceptance. All integers are unsigned
and big-endian. No HTTP/3 or bulk data multiplexing is used.

Control frames have a 6-byte header: version (u8, always 4), type (u8), payload length
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
| PING | 7 | id u64 |
| PONG | 8 | id u64 |

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

Content kinds are file=1 (`application/octet-stream`) and text=2 (`text/plain`).
The former URL kind 3 is rejected, along with other unknown kinds. File offers
require a safe filename (1–255 bytes). Text offers require an empty filename and
at most 65,536 payload bytes, including empty text. OFFER payload length is
51–306 bytes. Text stays on the payload stream, never in control metadata.

Text is accumulated in bounded memory, validated after exact size/EOF/SHA-256,
and returned only after the acknowledgement exchange. No text file is created.
Strict UTF-8 permits tabs, CR and LF, but excludes NUL, other C0/C1 controls, and
DEL. URLs are plain text and are never opened. File-only receivers reject text
with error 2 before accepting payload bytes.

An authenticated connectivity check opens one control stream, sends PING with a
random nonzero ID, and finishes that direction. The receiver requires EOF, replies
with PONG carrying the same ID, then finishes its direction. The checker requires
the matching PONG and EOF. No offer, payload, received file, or completion
notification is produced. Apps/CLI continue reception after this connection.
Normal pinned TLS identity, deadlines, cancellation, and pre-authentication route
fallback apply. A successful probe means the paired Beam receiver answered; it
is not a discovery hint or an ICMP response. A failed probe does not change trust.

| Local state | Allowed next event |
| --- | --- |
| Sender waiting for acceptance | ACCEPT, REJECT, FAILED, or connection failure/cancellation |
| Sender streaming payload | Payload sends/FIN, or connection failure/cancellation |
| Sender waiting for verification | COMPLETE, REJECT, FAILED, or connection failure/cancellation |
| Receiver waiting for offer | OFFER, or connection failure/cancellation |
| Receiver receiving payload | Matching payload ID, exact bytes/FIN, or failure/cancellation |
| Receiver awaiting acknowledgement | ACKNOWLEDGED, REJECT, FAILED, or connection failure/cancellation |

Unexpected messages, IDs, stream directions, or trailing control bytes fail the
transfer. For file offers, the receiver writes into a private, exclusively created temporary file
relative to a held output-directory descriptor. Only after valid size, digest, EOF,
a cancellation check does it flush and link
the temporary file to its final name with no replacement. It then sends COMPLETE.
The sender sends ACKNOWLEDGED and
finishes the control send direction; the receiver finishes its direction after reading
the acknowledgement. The sender drains that final EOF before reporting success.

Progress callbacks report hashing, connecting/waiting, payload bytes, verification,
and complete on the calling thread. Payload updates include average bytes/second
since payload transfer began, excluding hashing/connection setup. Payload progress reaching its total does not
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
is part of v4. This is an evolving protocol, not a promised stable ABI.
