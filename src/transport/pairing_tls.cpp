#include "transport/pairing_tls.hpp"
#include "protocol/pairing.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mutex>
#include <net/if.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <poll.h>
#include <psa/crypto.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

#if !defined(MBEDTLS_KEY_EXCHANGE_ECJPAKE_ENABLED) || !defined(MBEDTLS_THREADING_PTHREAD)
#error "Reprovision the pairing dependency with make pairing-deps (Android: make android-deps)"
#endif

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
void tls_check(int code) {
    if (code == 0)
        return;
    // Library error text carries no peer payload, credential, or PIN.
    std::array<char, 256> error{};
    mbedtls_strerror(code, error.data(), error.size());
    throw std::runtime_error(std::string("Pairing authentication failed: ") + error.data());
}
int send_callback(void* context, const unsigned char* bytes, std::size_t size) noexcept {
    const auto& socket = *static_cast<Socket*>(context);
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto count = ::send(socket.fd, bytes, size, flags);
    if (count >= 0)
        return static_cast<int>(count);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}
int receive_callback(void* context, unsigned char* bytes, std::size_t size) noexcept {
    const auto count = recv(static_cast<Socket*>(context)->fd, bytes, size, 0);
    if (count >= 0)
        return static_cast<int>(count);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
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
    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config config{};
    mbedtls_x509_crt own{}, peer{};
    mbedtls_pk_context key{};
    std::string fingerprint;
    std::stop_token stop;
    Deadline deadline;
    Impl(Socket value, std::stop_token token, Deadline limit)
        : socket(std::move(value)), stop(token), deadline(limit) {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&config);
        mbedtls_x509_crt_init(&own);
        mbedtls_x509_crt_init(&peer);
        mbedtls_pk_init(&key);
    }
    ~Impl() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
        mbedtls_x509_crt_free(&own);
        mbedtls_x509_crt_free(&peer);
        mbedtls_pk_free(&key);
    }
    void again(int result) {
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
            wait(socket.fd, result == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, stop, deadline);
        else
            tls_check(result);
    }
    void io(std::span<char> bytes, bool sending) {
        while (!bytes.empty()) {
            check(stop, deadline);
            const int result =
                sending
                    ? mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(bytes.data()),
                                        bytes.size())
                    : mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char*>(bytes.data()),
                                       bytes.size());
            if (result > 0)
                bytes = bytes.subspan(static_cast<std::size_t>(result));
            else {
                checked(result != 0, "Pairing connection closed");
                again(result);
            }
        }
    }
};
Tls::Tls(Socket socket, bool server, std::string_view password, const Credentials& credentials,
         const DeviceIdentity& peer, std::stop_token stop, Deadline deadline)
    : impl(std::make_unique<Impl>(std::move(socket), stop, deadline)) {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        checked(psa_crypto_init() == PSA_SUCCESS, "Initialize pairing cryptography failed");
    });
    auto& p = *impl;
    tls_check(mbedtls_ssl_config_defaults(
        &p.config, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT));
    mbedtls_ssl_conf_session_tickets(&p.config, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
    mbedtls_ssl_conf_new_session_tickets(&p.config, 0);
#if defined(MBEDTLS_SSL_RENEGOTIATION)
    mbedtls_ssl_conf_renegotiation(&p.config, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
    const bool pake = !password.empty();
    if (pake) {
        static const int suites[]{MBEDTLS_TLS_ECJPAKE_WITH_AES_128_CCM_8, 0};
        mbedtls_ssl_conf_min_tls_version(&p.config, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&p.config, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_ciphersuites(&p.config, suites);
        // Authentication is EC J-PAKE, not a certificate handshake.
        mbedtls_ssl_conf_authmode(&p.config, MBEDTLS_SSL_VERIFY_NONE);
    } else {
        mbedtls_ssl_conf_min_tls_version(&p.config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&p.config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_tls13_key_exchange_modes(&p.config,
                                                  MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);
        mbedtls_ssl_conf_authmode(&p.config, MBEDTLS_SSL_VERIFY_REQUIRED);
        tls_check(mbedtls_x509_crt_parse_file(&p.own, credentials.certificate.c_str()));
        tls_check(mbedtls_pk_parse_keyfile(&p.key, credentials.private_key.c_str(), nullptr));
        tls_check(mbedtls_x509_crt_parse(
            &p.peer, reinterpret_cast<const unsigned char*>(peer.certificate_pem.c_str()),
            peer.certificate_pem.size() + 1));
        mbedtls_ssl_conf_ca_chain(&p.config, &p.peer, nullptr);
        tls_check(mbedtls_ssl_conf_own_cert(&p.config, &p.own, &p.key));
        p.fingerprint = peer.fingerprint;
        mbedtls_ssl_conf_verify(
            &p.config,
            +[](void* context, mbedtls_x509_crt* certificate, int depth,
                std::uint32_t* flags) -> int {
                if (depth != 0) {
                    *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
                    return 0;
                }
                std::array<unsigned char, 32> digest{};
                unsigned count = 0;
                if (EVP_Digest(certificate->raw.p, certificate->raw.len, digest.data(), &count,
                               EVP_sha256(), nullptr) != 1 ||
                    count != 32) {
                    *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
                    return 0;
                }
                const auto& expected = static_cast<Impl*>(context)->fingerprint;
                bool same = expected.size() == 64;
                for (std::size_t i = 0; same && i < 32; ++i)
                    same = expected[i * 2] == "0123456789abcdef"[digest[i] >> 4] &&
                           expected[i * 2 + 1] == "0123456789abcdef"[digest[i] & 15];
                if (!same)
                    *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
                return 0;
            },
            &p);
    }
    tls_check(mbedtls_ssl_setup(&p.ssl, &p.config));
    if (pake)
        tls_check(mbedtls_ssl_set_hs_ecjpake_password(
            &p.ssl, reinterpret_cast<const unsigned char*>(password.data()), password.size()));
    else
        tls_check(mbedtls_ssl_set_hostname(&p.ssl, peer.tls_name.c_str()));
    mbedtls_ssl_set_bio(&p.ssl, &p.socket, send_callback, receive_callback, nullptr);
    for (;;) {
        check(stop, deadline);
        const int result = mbedtls_ssl_handshake(&p.ssl);
        if (result == 0)
            break;
        p.again(result);
    }
}
Tls::~Tls() = default;
bool Tls::readable() const {
    if (mbedtls_ssl_check_pending(&impl->ssl))
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
