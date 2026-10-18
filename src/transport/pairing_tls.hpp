#pragma once
#include "beam/identity.hpp"
#include <memory>

namespace beam::pairing_transport {
using Deadline = std::chrono::steady_clock::time_point;
void check(std::stop_token stop, Deadline deadline);
class Socket {
  public:
    int fd{-1};
    std::string remote_host;
    Socket() = default;
    explicit Socket(int descriptor);
    ~Socket();
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};
struct Listener {
    Socket socket;
    std::uint16_t port;
    explicit Listener(std::uint16_t port);
    Socket accept(std::stop_token stop, Deadline deadline);
};
Socket connect(const Endpoint& endpoint, std::stop_token stop, Deadline deadline);
void send_hello(Socket& socket, const std::vector<std::string>& fields, std::stop_token stop,
                Deadline deadline);
std::vector<std::string> read_hello(Socket& socket, std::stop_token stop, Deadline deadline);
std::vector<std::string> local_addresses();
// The first connection is authenticated by EC J-PAKE. A second, fresh TLS 1.3
// connection uses exact peer certificates and mandatory mutual authentication.
class Tls {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Tls(Socket socket, bool server, std::string_view password, const Credentials& credentials,
        const DeviceIdentity& peer, std::stop_token stop, Deadline deadline);
    ~Tls();
    Tls(const Tls&) = delete;
    Tls& operator=(const Tls&) = delete;
    void send(const std::vector<std::string>& fields);
    std::vector<std::string> read();
    [[nodiscard]] bool readable() const;
    [[nodiscard]] std::string remote_host() const;
};
} // namespace beam::pairing_transport
