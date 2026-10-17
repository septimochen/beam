# Configuration

Desktop apps and CLI transfer commands read `~/.config/beam/config` at startup.
A missing file uses built-in defaults. Create the folder/file yourself if needed:

```ini
# One setting per line; comments occupy their own line.
receive_folder = "~/Downloads/Beam"
listen_port = 4269
discovery = true
timeout_seconds = 30
```

`receive_folder` must be a quoted absolute path or begin with `~/`. It must
already exist and pass the normal receive-directory safety checks. Config does
not create it. Omit this setting to use the desktop's remembered folder, or
`~/Downloads` when none is remembered. The desktop app creates the default Downloads
folder if missing. CLI receivers without a folder accept text/probes and reject files.

`listen_port` is 1–65535. `discovery` accepts `true`/`false` and controls receiver
LAN advertisement; it never affects pairing or TLS. `timeout_seconds` is 1–86400
and bounds individual transfer waits. Connectivity checks are capped at five
seconds per wait. Defaults are 4269, true, and 30 respectively.

Explicit command-line options override the corresponding configured value.
Desktop folder priority is `--output`, config, the remembered folder, then
`~/Downloads`. The folder picker remembers its selection for launches without a
configured override. `--no-discovery` always disables advertisement. Restart the
app or CLI receiver after editing the file. Duplicate/unknown keys, invalid values, unreadable files, NULs,
and files larger than 16 KiB fail with an error. No environment expansion, includes,
shell commands, keys, certificates, or trust records belong in this file.

If you change a listen port, update this device's saved endpoint on its peers.

Android uses **Settings** with the same default port and timeout. Its receive
folder is a single private subfolder name, initially `received`; it cannot point
outside Beam's private storage or at identity/outgoing staging storage. Stop
transfers, reception, and checks before saving. Settings survive relaunch; existing
files remain in their original folder. Export still requires **Save a received
file…**. Android has no desktop discovery backend or `~/.config` location.
