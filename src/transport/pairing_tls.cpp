#include "transport/pairing_tls.hpp"
#include "protocol/pairing.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
// Required for IFF_*; glibc exposes these as self-referential enum macros.
#include <net/if.h> // IWYU pragma: keep
#include <netdb.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace beam::pairing_transport {
void check(std::stop_token stop, Deadline deadline) {
    if (stop.stop_requested())
        throw TransferError(TransferErrorCode::cancelled, "Pairing cancelled");
    if (std::chrono::steady_clock::now() >= deadline)
        throw TransferError(TransferErrorCode::timeout, "Pairing expired");
}
namespace {
void checked(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void wait(int fd, short events, std::stop_token stop, Deadline deadline) {
    for (;;) {
        check(stop, deadline);
        pollfd item{fd, events, 0};
        const int result = poll(&item, 1, 50);
        if (result < 0 && errno == EINTR)
            continue;
        checked(result >= 0, "Pairing socket poll failed");
        if (result) {
            checked((item.revents & (POLLERR | POLLNVAL)) == 0, "Pairing socket failed");
            return;
        }
    }
}
std::string host_of(const sockaddr* address, socklen_t size) {
    std::array<char, NI_MAXHOST> text{};
    checked(getnameinfo(address, size, text.data(), static_cast<socklen_t>(text.size()), nullptr, 0,
                        NI_NUMERICHOST) == 0,
            "Read pairing address failed");
    std::string host(text.data());
    if (host.starts_with("::ffff:"))
        host.erase(0, 7);
    return host;
}
void raw_io(Socket& socket, std::span<char> bytes, bool sending, std::stop_token stop,
            Deadline deadline) {
    while (!bytes.empty()) {
        wait(socket.fd, sending ? POLLOUT : POLLIN, stop, deadline);
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto count = sending ? ::send(socket.fd, bytes.data(), bytes.size(), flags)
                                   : recv(socket.fd, bytes.data(), bytes.size(), 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        checked(count > 0, "Pairing connection closed");
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
}
std::array<char, 4> length_bytes(std::size_t size) {
    std::array<char, 4> out{};
    for (unsigned i = 0; i < 4; ++i)
        out[i] = static_cast<char>((size >> (24 - 8 * i)) & 255);
    return out;
}
std::size_t read_length(const std::array<char, 4>& bytes) {
    std::size_t size = 0;
    for (const auto c : bytes)
        size = (size << 8) | static_cast<unsigned char>(c);
    checked(size > 0 && size <= pairing_protocol::max_frame, "Invalid pairing frame length");
    return size;
}
// A socket BIO keeps writes safe from SIGPIPE without changing process signal handlers.
int bio_write(BIO* bio, const char* bytes, int size) noexcept {
    BIO_clear_retry_flags(bio);
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto count = ::send(static_cast<Socket*>(BIO_get_data(bio))->fd, bytes,
                              static_cast<std::size_t>(size), flags);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        BIO_set_retry_write(bio);
    return static_cast<int>(count);
}
int bio_read(BIO* bio, char* bytes, int size) noexcept {
    BIO_clear_retry_flags(bio);
    const auto count =
        recv(static_cast<Socket*>(BIO_get_data(bio))->fd, bytes, static_cast<std::size_t>(size), 0);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        BIO_set_retry_read(bio);
    return static_cast<int>(count);
}
BIO_METHOD* socket_method() {
    static const std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> method(
        [] {
            std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> value(
                BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "Beam pairing socket"),
                BIO_meth_free);
            checked(value && BIO_meth_set_write(value.get(), bio_write) == 1 &&
                        BIO_meth_set_read(value.get(), bio_read) == 1 &&
                        BIO_meth_set_ctrl(
                            value.get(),
                            +[](BIO*, int command, long, void*) noexcept -> long {
                                return command == BIO_CTRL_FLUSH ? 1 : 0;
                            }) == 1,
                    "Create pairing socket BIO failed");
            return value.release();
        }(),
        BIO_meth_free);
    return method.get();
}
} // namespace
Socket::Socket(int descriptor) : fd(descriptor) {
    checked(fd >= 0, "Create pairing socket failed");
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        close(fd);
        fd = -1;
        throw std::runtime_error("Configure pairing socket failed");
    }
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        close(fd);
        fd = -1;
        throw std::runtime_error("Configure pairing socket failed");
    }
#endif
}
Socket::~Socket() {
    if (fd >= 0)
        close(fd);
}
Socket::Socket(Socket&& other) noexcept
    : fd(std::exchange(other.fd, -1)), remote_host(std::move(other.remote_host)) {}
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (fd >= 0)
            close(fd);
        fd = std::exchange(other.fd, -1);
        remote_host = std::move(other.remote_host);
    }
    return *this;
}
Listener::Listener(std::uint16_t requested)
    : socket(::socket(AF_INET6, SOCK_STREAM, 0)), port(requested) {
    const int disabled = 0;
    checked(setsockopt(socket.fd, IPPROTO_IPV6, IPV6_V6ONLY, &disabled, sizeof(disabled)) == 0,
            "Enable dual-stack pairing listener failed");
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(requested);
    checked(bind(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                listen(socket.fd, 4) == 0,
            "Bind pairing listener failed");
    socklen_t size = sizeof(address);
    checked(getsockname(socket.fd, reinterpret_cast<sockaddr*>(&address), &size) == 0,
            "Read pairing listener port failed");
    port = ntohs(address.sin6_port);
}
Socket Listener::accept(std::stop_token stop, Deadline deadline) {
    for (;;) {
        wait(socket.fd, POLLIN, stop, deadline);
        sockaddr_storage address{};
        socklen_t size = sizeof(address);
        const int fd = ::accept(socket.fd, reinterpret_cast<sockaddr*>(&address), &size);
        if (fd < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        Socket result(fd);
        result.remote_host = host_of(reinterpret_cast<sockaddr*>(&address), size);
        return result;
    }
}
Socket connect(const Endpoint& endpoint, std::stop_token stop, Deadline deadline) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* raw = nullptr;
    checked(getaddrinfo(endpoint.host.c_str(), std::to_string(endpoint.port).c_str(), &hints,
                        &raw) == 0,
            "Invalid pairing endpoint");
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> address(raw, freeaddrinfo);
    Socket socket(::socket(raw->ai_family, SOCK_STREAM, 0));
    socket.remote_host = endpoint.host;
    const int result = ::connect(socket.fd, raw->ai_addr, raw->ai_addrlen);
    checked(result == 0 || errno == EINPROGRESS, "Connect to pairing host failed");
    wait(socket.fd, POLLOUT, stop, deadline);
    int error = 0;
    socklen_t size = sizeof(error);
    checked(getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0,
            "Connect to pairing host failed");
    return socket;
}
void send_hello(Socket& socket, const std::vector<std::string>& fields, std::stop_token stop,
                Deadline deadline) {
    auto body = pairing_protocol::pack(fields);
    auto length = length_bytes(body.size());
    raw_io(socket, length, true, stop, deadline);
    raw_io(socket, body, true, stop, deadline);
}
std::vector<std::string> read_hello(Socket& socket, std::stop_token stop, Deadline deadline) {
    std::array<char, 4> length{};
    raw_io(socket, length, false, stop, deadline);
    const auto count = read_length(length);
    checked(count <= 256, "Pairing hello exceeds bounds");
    std::string body(count, '\0');
    raw_io(socket, body, false, stop, deadline);
    return pairing_protocol::unpack(body);
}
std::vector<std::string> local_addresses() {
    ifaddrs* raw = nullptr;
    checked(getifaddrs(&raw) == 0, "Read LAN addresses failed");
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(raw, freeifaddrs);
    std::vector<std::string> result;
    for (auto* item = raw; item; item = item->ifa_next) {
        if (!item->ifa_addr || !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_MULTICAST) ||
            (item->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)))
            continue;
        const auto family = item->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6)
            continue;
        auto host =
            host_of(item->ifa_addr, family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
        // A sender's interface index is not meaningful on the scanning device.
        if (host.starts_with("fe80:") || host.find('%') != std::string::npos)
            continue;
        if (std::find(result.begin(), result.end(), host) == result.end() && result.size() < 8)
            result.push_back(std::move(host));
    }
    checked(!result.empty(), "No local Wi-Fi/Ethernet address; connect to a LAN first");
    return result;
}
struct Tls::Impl {
    Socket socket;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> config{nullptr, SSL_CTX_free};
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl{nullptr, SSL_free};
    std::array<unsigned char, 32> secret{};
    std::string fingerprint;
    std::stop_token stop;
    Deadline deadline;
    Impl(Socket value, std::stop_token token, Deadline limit)
        : socket(std::move(value)), stop(token), deadline(limit) {}
    ~Impl() { OPENSSL_cleanse(secret.data(), secret.size()); }
    // External PSK sessions describe this handshake only; they are never cached.
    static int session(SSL* connection, SSL_SESSION** output) noexcept {
        *output = nullptr;
        auto* self = static_cast<Impl*>(SSL_get_app_data(connection));
        const unsigned char cipher_id[]{0x13, 0x01}; // TLS_AES_128_GCM_SHA256
        const auto* cipher = SSL_CIPHER_find(connection, cipher_id);
        SSL_SESSION* value = SSL_SESSION_new();
        if (!value || !cipher ||
            SSL_SESSION_set1_master_key(value, self->secret.data(), self->secret.size()) != 1 ||
            SSL_SESSION_set_cipher(value, cipher) != 1 ||
            SSL_SESSION_set_protocol_version(value, TLS1_3_VERSION) != 1) {
            SSL_SESSION_free(value);
            return 0;
        }
        *output = value;
        return 1;
    }
    static int client_psk(SSL* connection, const EVP_MD* digest, const unsigned char** identity,
                          std::size_t* size, SSL_SESSION** output) noexcept {
        *output = nullptr;
        if (digest && EVP_MD_type(digest) != NID_sha256)
            return 0;
        *identity = reinterpret_cast<const unsigned char*>("beam-pair/2");
        *size = 11;
        return session(connection, output);
    }
    static int server_psk(SSL* connection, const unsigned char* identity, std::size_t size,
                          SSL_SESSION** output) noexcept {
        *output = nullptr;
        if (size != 11 || CRYPTO_memcmp(identity, "beam-pair/2", size) != 0)
            return 0;
        return session(connection, output);
    }
    static int verify(int valid, X509_STORE_CTX* store) noexcept {
        const auto* connection = static_cast<SSL*>(
            X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx()));
        const auto* self = static_cast<Impl*>(SSL_get_app_data(connection));
        if (!valid || X509_STORE_CTX_get_error_depth(store) != 0)
            return 0;
        std::array<unsigned char, 32> digest{};
        unsigned count = 0;
        if (X509_digest(X509_STORE_CTX_get_current_cert(store), EVP_sha256(), digest.data(),
                        &count) != 1 ||
            count != 32 || self->fingerprint.size() != 64)
            return 0;
        std::array<char, 64> hex{};
        for (std::size_t i = 0; i < digest.size(); ++i) {
            hex[2 * i] = "0123456789abcdef"[digest[i] >> 4];
            hex[2 * i + 1] = "0123456789abcdef"[digest[i] & 15];
        }
        return CRYPTO_memcmp(hex.data(), self->fingerprint.data(), hex.size()) == 0;
    }
    void again(int result) {
        const int error = SSL_get_error(ssl.get(), result);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
            wait(socket.fd, error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, stop, deadline);
        else
            throw std::runtime_error("Pairing authentication failed or connection closed");
    }
    void io(std::span<char> bytes, bool sending) {
        while (!bytes.empty()) {
            check(stop, deadline);
            ERR_clear_error();
            std::size_t count = 0;
            const int result = sending ? SSL_write_ex(ssl.get(), bytes.data(), bytes.size(), &count)
                                       : SSL_read_ex(ssl.get(), bytes.data(), bytes.size(), &count);
            if (result == 1) {
                checked(count > 0, "Pairing connection closed");
                bytes = bytes.subspan(count);
            } else
                again(result);
        }
    }
};
Tls::Tls(Socket socket, bool server, std::string_view secret_hex, const Credentials& credentials,
         const DeviceIdentity& peer, std::stop_token stop, Deadline deadline)
    : impl(std::make_unique<Impl>(std::move(socket), stop, deadline)) {
    auto& p = *impl;
    p.config.reset(SSL_CTX_new(TLS_method()));
    checked(p.config && SSL_CTX_set_min_proto_version(p.config.get(), TLS1_3_VERSION) == 1 &&
                SSL_CTX_set_max_proto_version(p.config.get(), TLS1_3_VERSION) == 1 &&
                SSL_CTX_set_num_tickets(p.config.get(), 0) == 1 &&
                SSL_CTX_set_max_early_data(p.config.get(), 0) == 1,
            "Configure pairing TLS failed");
    SSL_CTX_set_options(p.config.get(), SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_clear_options(p.config.get(), SSL_OP_ALLOW_NO_DHE_KEX);
    SSL_CTX_set_session_cache_mode(p.config.get(), SSL_SESS_CACHE_OFF);
    const bool bootstrap = !secret_hex.empty();
    if (bootstrap) {
        checked(pairing_protocol::hex_string(secret_hex, 64), "Invalid invitation secret");
        const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        for (std::size_t i = 0; i < p.secret.size(); ++i)
            p.secret[i] = static_cast<unsigned char>((nibble(secret_hex[2 * i]) << 4) |
                                                     nibble(secret_hex[2 * i + 1]));
        checked(SSL_CTX_set_ciphersuites(p.config.get(), "TLS_AES_128_GCM_SHA256") == 1,
                "Configure invitation cipher failed");
        SSL_CTX_set_psk_use_session_callback(p.config.get(), Impl::client_psk);
        SSL_CTX_set_psk_find_session_callback(p.config.get(), Impl::server_psk);
    } else {
        p.fingerprint = peer.fingerprint;
        std::unique_ptr<BIO, decltype(&BIO_free)> input(
            BIO_new_mem_buf(peer.certificate_pem.data(),
                            static_cast<int>(peer.certificate_pem.size())),
            BIO_free);
        std::unique_ptr<X509, decltype(&X509_free)> certificate(
            input ? PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
        checked(certificate &&
                    X509_STORE_add_cert(SSL_CTX_get_cert_store(p.config.get()),
                                        certificate.get()) == 1 &&
                    SSL_CTX_use_certificate_file(p.config.get(), credentials.certificate.c_str(),
                                                 SSL_FILETYPE_PEM) == 1 &&
                    SSL_CTX_use_PrivateKey_file(p.config.get(), credentials.private_key.c_str(),
                                                SSL_FILETYPE_PEM) == 1 &&
                    SSL_CTX_check_private_key(p.config.get()) == 1,
                "Load pairing certificates failed");
        SSL_CTX_set_verify(p.config.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           Impl::verify);
        SSL_CTX_set_verify_depth(p.config.get(), 0);
    }
    p.ssl.reset(SSL_new(p.config.get()));
    checked(p.ssl != nullptr, "Create pairing TLS failed");
    SSL_set_app_data(p.ssl.get(), &p);
    if (!bootstrap) {
        SSL_set_hostflags(p.ssl.get(), X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
        checked(
            X509_VERIFY_PARAM_set1_host(SSL_get0_param(p.ssl.get()), peer.tls_name.c_str(), 0) == 1,
            "Configure pairing identity name failed");
    }
    BIO* bio = BIO_new(socket_method());
    checked(bio != nullptr, "Create pairing socket BIO failed");
    BIO_set_data(bio, &p.socket);
    BIO_set_init(bio, 1);
    SSL_set_bio(p.ssl.get(), bio, bio);
    if (server)
        SSL_set_accept_state(p.ssl.get());
    else
        SSL_set_connect_state(p.ssl.get());
    for (;;) {
        check(stop, deadline);
        ERR_clear_error();
        const int result = SSL_do_handshake(p.ssl.get());
        if (result == 1)
            break;
        p.again(result);
    }
    checked(SSL_get_negotiated_group(p.ssl.get()) != 0, "Pairing requires ephemeral key exchange");
    if (bootstrap)
        checked(SSL_session_reused(p.ssl.get()) == 1, "Invitation authentication required");
    else
        checked(SSL_get_verify_result(p.ssl.get()) == X509_V_OK,
                "Pairing certificate verification failed");
}
Tls::~Tls() = default;
bool Tls::readable() const {
    if (SSL_pending(impl->ssl.get()) > 0)
        return true;
    pollfd item{impl->socket.fd, POLLIN, 0};
    return poll(&item, 1, 0) > 0;
}
void Tls::send(const std::vector<std::string>& fields) {
    auto body = pairing_protocol::pack(fields);
    auto length = length_bytes(body.size());
    impl->io(length, true);
    impl->io(body, true);
}
std::vector<std::string> Tls::read() {
    std::array<char, 4> length{};
    impl->io(length, false);
    std::string body(read_length(length), '\0');
    impl->io(body, false);
    return pairing_protocol::unpack(body);
}
std::string Tls::remote_host() const { return impl->socket.remote_host; }
} // namespace beam::pairing_transport
