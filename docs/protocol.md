# Beam protocol v1 (Phase 1)

ALPN: `beam/1`. QUIC/TLS authenticates both peers before any application stream.
One connection carries exactly one bidirectional control stream opened by the sender,
and one unidirectional payload stream opened after acceptance. All integers are unsigned
and big-endian. No HTTP/3 or bulk data multiplexing is used.

Control frames have a 6-byte header: version (u8, always 1), type (u8), payload length
(u32, at most 1024). A decoder rejects unsupported versions/types, truncated fields,
invalid lengths/text, zero IDs, and trailing bytes. Reads handle arbitrary stream
fragmentation; an oversized header fails before allocating its payload.

| Type | Value | Payload |
| --- | --- | --- |
| OFFER | 1 | id u64, file size u64, SHA-256 32 bytes, filename length u16, filename bytes |
| ACCEPT | 2 | id u64 |
| REJECT | 3 | id u64, reason length u16, printable ASCII reason bytes (1–256) |
| COMPLETE | 4 | id u64 |
| FAILED | 5 | id u64, reason length u16, printable ASCII reason bytes (1–256) |
| ACKNOWLEDGED | 6 | id u64 |

The sender generates a random nonzero ID and hashes the input through a 64 KiB buffer.
It sends OFFER and waits for ACCEPT with the same ID. A rejected destination receives
REJECT. The payload stream begins with id u64 followed by exactly the offered bytes
and a QUIC FIN. The receiver rejects extra/missing bytes, wrong IDs, and incorrect hashes.
A filename must pass Beam's portable basename validator; it cannot supply a path.

The receiver writes into a private, exclusively created temporary file relative to a
held output-directory descriptor. Only after valid size, digest, and EOF does it flush
and link the temporary file to its final name with no replacement. It then sends COMPLETE.
The sender sends ACKNOWLEDGED and finishes the control send direction; the receiver
finishes its direction after reading the acknowledgement. The sender drains that final
EOF before reporting success. Either side closes the connection on protocol errors.

Disk errors/invalid payloads after acceptance cause FAILED when the control stream
remains usable. Local errors retain context; remote errors are generic to avoid
exposing receiver filesystem details. A lost completion exchange can leave a verified
file with a failed sender result; retries never overwrite it.

No resume, concurrency, cancellation command, MIME metadata, Unicode filenames,
discovery, or persistent connection reuse is part of v1. Timeouts bound individual
waits, not total transfer time. This is an initial protocol, not a promised stable ABI.
