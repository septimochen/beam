# TLS setup

For everyday use, follow [device identity and pairing](pairing.md). Beam creates
persistent device certificates and trusts only explicitly paired certificates.
The instructions below preserve the original dedicated-CA workflow.

## Dedicated-CA transfers

Beam uses MsQuic's TLS 1.3 certificate validation with mandatory client authentication.
An additional OpenSSL X.509 verification step restricts peers to the supplied CA even
if another root is available in the TLS system store. Use a dedicated private CA
for the devices permitted to send/receive. Any certificate issued by this CA with
the appropriate TLS usage is authorized; this legacy mode does not use the stored
peer allowlist. Keep the CA signing key off the transfer devices.
Exchange the CA certificate through a trusted channel and verify its SHA-256 fingerprint
out of band. Do not use an organizational/public CA that also authorizes unrelated clients.

For a local demo, generate a CA and two certificates in a private directory outside
source control. The CA key is needed only for issuance, never by `beam`:

```sh
mkdir beam-credentials
cd beam-credentials
umask 077
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem \
  -days 30 -subj /CN=Beam-Private-CA
openssl x509 -in ca.pem -noout -fingerprint -sha256
```

Generate the receiver certificate. Replace the SAN IP with its reachable LAN IP, and
replace the DNS name if desired. For localhost tests keep `127.0.0.1`:

```sh
openssl req -newkey rsa:2048 -nodes -keyout server.key -out server.csr -subj /CN=beam-receiver
cat > server.ext <<'EXT'
basicConstraints=CA:FALSE
keyUsage=digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1
EXT
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
  -out server.pem -days 30 -extfile server.ext
```

Generate the sender certificate:

```sh
openssl req -newkey rsa:2048 -nodes -keyout client.key -out client.csr -subj /CN=beam-sender
cat > client.ext <<'EXT'
basicConstraints=CA:FALSE
keyUsage=digitalSignature
extendedKeyUsage=clientAuth
EXT
openssl x509 -req -in client.csr -CA ca.pem -CAkey ca.key -CAserial ca.srl \
  -out client.pem -days 30 -extfile client.ext
```

Copy only `server.pem`, `server.key`, and `ca.pem` to the receiver, and only
`client.pem`, `client.key`, and `ca.pem` to the sender using a secure provisioning
channel. Private keys must be readable only by their owner. Replace expired or
compromised credentials. The CLI requires PEM certificate/key files and an
OpenSSL/quictls-backed MsQuic build; Schannel credentials are not implemented.

The flags are based on the upstream [MsQuic credential API](https://github.com/microsoft/msquic/blob/v2.6.2/docs/api/QUIC_CREDENTIAL_CONFIG.md).
