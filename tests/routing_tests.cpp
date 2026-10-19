#include "beam/routing.hpp"
#include "protocol/codec.hpp"
#include <array>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
struct Socket {
    int value{-1};
    Socket() = default;
    ~Socket() {
        if (value >= 0)
            close(value);
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};
void require(bool condition, const char* message) {
    if (!condition)
        throw std::logic_error(message);
}
template <typename Action> void rejects(Action action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::logic_error("invalid route was accepted");
}
void unit_tests() {
    using beam::RoutePreference;
    beam::TrustedPeer peer{"bob", "192.168.1.2:4269", {}, "[FD7A:115C:A1E0::2]:4269"};
    auto endpoints = beam::peer_endpoints(peer);
    require(endpoints.size() == 2 && endpoints[0].host == "192.168.1.2" &&
                endpoints[1].host == "fd7a:115c:a1e0::2",
            "direct route was not preferred");
    require(beam::peer_endpoints(peer, RoutePreference::direct).size() == 1 &&
                beam::peer_endpoints(peer, RoutePreference::tailscale)[0].host ==
                    "fd7a:115c:a1e0::2",
            "explicit route preference failed");
    peer.endpoint = "[fd7a:115c:a1e0:0:0:0:0:2]:4269";
    require(beam::peer_endpoints(peer).size() == 1, "equivalent routes were not deduplicated");
    require(beam::peer_endpoints(peer, RoutePreference::direct).empty(),
            "Tailscale route classified as direct");
    peer.endpoint = "100.64.0.20:4269";
    peer.tailscale_endpoint = "[::ffff:100.64.0.20]:4269";
    endpoints = beam::peer_endpoints(peer);
    require(endpoints.size() == 1 && endpoints.front().host == "100.64.0.20",
            "mapped IPv4 route was not normalized and deduplicated");
    require(beam::endpoint_string(beam::numeric_endpoint("[::ffff:127.0.0.1]:4269")) ==
                "127.0.0.1:4269",
            "mapped loopback was not normalized");
    peer.endpoint.clear();
    peer.tailscale_endpoint.clear();
    require(beam::peer_endpoints(peer).empty(), "empty peer invented a route");
    peer.tailscale_endpoint = "127.0.0.1:4269";
    rejects([&] { static_cast<void>(beam::peer_endpoints(peer)); });
    for (const auto* host :
         {"100.64.0.1", "100.127.255.254", "fd7a:115c:a1e0::1", "::ffff:100.64.0.2"})
        require(beam::is_tailscale_address(host), "Tailscale prefix missed");
    for (const auto* host : {"100.63.255.255", "100.128.0.0", "10.0.0.1", "fd7a:115c:a1e1::1",
                             "fd00::1", "::1", "example.ts.net", "fd7a:115c:a1e0::1%2"})
        require(!beam::is_tailscale_address(host), "non-Tailscale address accepted");
    const std::string hidden = std::string("100.64.0.1") + '\0' + "hidden";
    require(!beam::is_tailscale_address(hidden), "NUL hidden from C address parser");
    rejects([&] { static_cast<void>(beam::numeric_endpoint(hidden + ":4269")); });
    for (const auto* text :
         {"example.ts.net:4269", "127.0.0.1:0", "[::1%0]:4269", "[::1%name]:4269",
          "[::1%4294967296]:4269", "[::1%2%3]:4269", "127.0.0.1 :4269"})
        rejects([&] { static_cast<void>(beam::numeric_endpoint(text)); });
    require(beam::endpoint_string(beam::numeric_endpoint("[FE80::1%002]:04269")) ==
                "[fe80::1%2]:4269",
            "scope/address normalization failed");
}
void runtime_tests(int argc, char** argv) {
    require(argc == 7, "expected mode, state, peer, primary, fallback and source");
    const std::string mode = argv[1];
    beam::TransferOptions options;
    std::string tls_name;
    {
        beam::IdentityStore store(argv[2]);
        options.credentials = store.credentials(argv[3]);
        tls_name = store.peer(argv[3]).identity.tls_name;
    }
    const auto primary = beam::numeric_endpoint(argv[4]);
    options.fallback_endpoints.push_back(beam::numeric_endpoint(argv[5]));
    options.timeout = std::chrono::seconds(3);
    options.connect_timeout = std::chrono::milliseconds(500);
    std::vector<beam::Endpoint> attempts;
    options.on_connecting = [&](const auto& endpoint) {
        attempts.push_back(endpoint);
        if (mode == "callback-error")
            throw std::logic_error("callback failed");
    };
    if (mode == "hold-completion") {
        options.timeout = std::chrono::seconds(5);
        options.on_listening = [] { std::cout << "Listening" << std::endl; };
        auto connection = beam::transport::listen(primary.port, options);
        auto control = connection->accept(false);
        const auto offer = beam::protocol::read(*control);
        beam::protocol::write(*control, {beam::protocol::Type::accept, offer.id, 0, {}, {}});
        auto payload = connection->accept(true);
        std::array<std::byte, 65536> bytes{};
        while (payload->read(bytes)) {
        }
        try {
            (void)beam::protocol::read(*control);
        } catch (const beam::TransferError& error) {
            require(error.code == beam::TransferErrorCode::timeout ||
                        error.code == beam::TransferErrorCode::transport,
                    "sender did not abort timeout");
            return;
        }
        throw std::logic_error("sender sent unexpected data");
    }
    Socket blackhole;
    if (mode == "cancel-connect" || mode == "callback-wait") {
        blackhole.value = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(primary.port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(blackhole.value >= 0 && bind(blackhole.value, reinterpret_cast<sockaddr*>(&address),
                                             sizeof(address)) == 0,
                "cannot bind cancellation blackhole");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        options.should_cancel = [deadline, mode] {
            const bool elapsed = std::chrono::steady_clock::now() >= deadline;
            if (elapsed && mode == "callback-wait")
                throw beam::TransferError(beam::TransferErrorCode::timeout, "callback timeout");
            return elapsed;
        };
    }
    try {
        if (mode == "fallback-text")
            beam::send_text(primary, "routed text", tls_name, options);
        else if (mode == "fallback-url")
            beam::send_text(primary, "https://example.com/", tls_name, options);
        else
            beam::send_file(primary, argv[6], tls_name, options);
        require(mode.starts_with("fallback-") && attempts.size() == 2,
                "unexpected fallback success");
    } catch (const beam::TransferError& error) {
        require(!mode.starts_with("fallback-") && attempts.size() == 1, "unsafe endpoint retry");
        if (mode == "cancel-connect")
            require(error.code == beam::TransferErrorCode::cancelled && !error.remote,
                    "wrong cancellation category");
        else if (mode == "callback-wait")
            require(error.code == beam::TransferErrorCode::timeout && !error.remote &&
                        std::string_view(error.what()) == "callback timeout",
                    "callback exception was treated as a connection deadline");
        else if (mode == "no-fallback-reject")
            require(error.code == beam::TransferErrorCode::destination_exists && error.remote,
                    "wrong rejection category");
        else if (mode == "no-fallback-late")
            require(error.code == beam::TransferErrorCode::timeout ||
                        error.code == beam::TransferErrorCode::transport,
                    "wrong post-payload timeout category");
        else
            require(mode == "no-fallback-auth" && error.code == beam::TransferErrorCode::transport,
                    "wrong authentication failure");
    } catch (const std::logic_error& error) {
        require(mode == "callback-error" && attempts.size() == 1 &&
                    std::string_view(error.what()) == "callback failed",
                "unexpected callback failure");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 1)
            unit_tests();
        else
            runtime_tests(argc, argv);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
