# Beam

Send a file directly between macOS and Linux devices from the command line.
Beam discovers receivers on your LAN, encrypts transfers with QUIC, and verifies
each file before reporting success. No account or cloud service is required.

Beam currently transfers one file at a time. Device pairing and a GUI are on
the [roadmap](docs/ROADMAP.md); for now, both devices need certificates.

## Quick start

### 1. Build Beam on both devices

You'll need CMake 3.25+, a C++23 compiler, Git, Perl, Make, and OpenSSL development
libraries. LAN discovery uses Bonjour on macOS or Avahi on Linux.
See [build setup](docs/development.md#build-setup) for platform dependencies.

```sh
git clone https://github.com/septimochen/beam.git
cd beam
make deps   # downloads and builds MsQuic; takes a few minutes
make build
./build/beam --help
```

The examples below run from the repository directory on each device.

### 2. Set up certificates

Follow [TLS setup](docs/tls.md) to create certificates signed by a dedicated
private CA. Put `server.pem`, `server.key`, and `ca.pem` on the receiver, and
`client.pem`, `client.key`, and `ca.pem` on the sender. Use their full paths in
the commands below if you store them outside the repository directory.
Keep private keys out of source control.

### 3. Start the receiver

On the device receiving the file:

```sh
mkdir -p received
./build/beam receive --listen 4269 --output ./received --name Laptop \
  --cert server.pem --key server.key --ca ca.pem
```

Allow UDP port 4269 through its firewall. The receiver advertises itself on the
LAN, accepts one file, and exits. Run it again to receive another file.

### 4. Find the receiver and send a file

On the sending device, with the receiver running:

```sh
./build/beam devices
```

Copy an `IP:PORT` endpoint from the output, then send your file:

```sh
./build/beam send 192.168.1.50:4269 hello.txt \
  --cert client.pem --key client.key --ca ca.pem
```

Replace `192.168.1.50:4269` with the receiver's endpoint and `hello.txt` with your
file. You can also use a known IP address directly without discovery.
The verified file appears in the receiver's `received` directory.

## Useful options and tips

- Add `--server-name my-laptop` to `send` if the receiver certificate names
  `my-laptop` instead of its IP address. Discovery does not verify a device's identity.
- Use `[IPv6]:4269` for an IPv6 endpoint.
- Add `--no-discovery` to `receive` to keep it unadvertised; omit `--name` in that case.
- Add `--timeout 120` to `send` or `receive` for longer individual waits
  (the default is 30 seconds). `devices --timeout 5` browses for five seconds.
- Press **Ctrl-C** to cancel. Add `--no-progress` to `send` or `receive` for scripts;
  progress goes to stderr and success to stdout.
- Beam refuses to overwrite existing files. If a transfer fails at the final
  acknowledgement, check the destination before retrying: a verified file may
  already be there. Keep the source file unchanged during transfer.
- Use a plain, printable ASCII filename; hidden names, reserved names, and
  symlinks are rejected.

If discovery finds nothing, check that the receiver is running on the same LAN
and Avahi is running on Linux. See [discovery troubleshooting](docs/discovery.md#verification-and-troubleshooting)
for more help.

## Contributing

Start with the [roadmap](docs/ROADMAP.md) and [development guide](docs/development.md).
Keep changes focused and include tests for behavior changes.

```sh
make format          # apply clang-format 23
make check           # check formatting, build, and run tests
make lint            # run clang-tidy and static analysis
make discovery-test  # test live LAN discovery; requires Bonjour/Avahi
```

See [build setup](docs/development.md#build-setup) for formatter and lint tool setup,
and [build options](docs/development.md#build-options) for Release and other configurations.
For deeper implementation details, read [architecture](docs/architecture.md),
[protocol](docs/protocol.md), and [security](docs/security.md).
