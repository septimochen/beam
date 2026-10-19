# MsQuic public API headers

These unmodified POSIX public headers come from Microsoft MsQuic v2.6.2,
commit `819ab74f851ee168504cbc392ec32e7bed1d82e9`:

- `src/inc/msquic.h`
- `src/inc/msquic_posix.h`
- `src/inc/quic_sal_stub.h`

Source: <https://github.com/microsoft/msquic/tree/v2.6.2/src/inc>.
The upstream MIT license is retained in `LICENSE` and in
`third_party/licenses/msquic.md` for installed applications.

Beam prefers installed SDK headers. These headers support Linux runtime-only
packages, which omit development headers. Beam still links the installed runtime;
no MsQuic or TLS implementation is built from this directory. Native configuration
validates API compatibility and requires MsQuic 2.x, version 2.4.0 or newer.
Android uses its own pinned per-ABI headers and archives.
