#include "beam/discovery.hpp"
#include "beam/identity.hpp"
#include "desktop/service.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
using namespace beam;
using namespace beam::desktop;
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
    throw std::logic_error("invalid desktop operation accepted");
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        std::string pattern =
            std::filesystem::canonical(std::filesystem::temp_directory_path()).string() +
            "/beam-desktop-XXXXXX";
        const auto* created = mkdtemp(pattern.data());
        if (!created)
            throw std::runtime_error("create desktop fixture failed");
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
};
struct Socket {
    int descriptor{socket(AF_INET, SOCK_DGRAM, 0)};
    std::uint16_t port{};
    Socket() {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(descriptor >= 0 &&
                    bind(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
                "cannot reserve desktop test port");
        socklen_t length = sizeof(address);
        require(getsockname(descriptor, reinterpret_cast<sockaddr*>(&address), &length) == 0,
                "cannot read test port");
        port = ntohs(address.sin_port);
    }
    void release() {
        close(descriptor);
        descriptor = -1;
    }
    ~Socket() {
        if (descriptor >= 0)
            close(descriptor);
    }
};
struct Mailbox {
    Service& service;
    std::vector<Event> pending;
    Event await(Direction direction, EventKind kind) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        do {
            for (auto& event : service.take_events())
                pending.push_back(std::move(event));
            const auto found =
                std::find_if(pending.begin(), pending.end(), [&](const Event& event) {
                    return event.direction == direction && event.kind == kind;
                });
            if (found != pending.end()) {
                auto result = *found;
                pending.erase(found);
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        for (const auto& event : pending)
            std::cerr << event.message << '\n';
        throw std::runtime_error("desktop event timed out");
    }
};
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
void tests(bool live_discovery) {
    TemporaryDirectory temporary;
    const auto root = temporary.path;
    std::ofstream(root / "sample.txt", std::ios::binary) << "desktop file bytes\n";
    validate_request({"bob", ContentKind::file, root / "sample.txt", {}});
    validate_request({"bob", ContentKind::text, {}, "hello\r\n世界"});
    validate_request({"bob", ContentKind::text, {}, "https://example.com/a"});
    rejects([&] { validate_request({"", ContentKind::text, {}, "private"}); });
    rejects([&] {
        validate_request({"bob", ContentKind::text, {}, std::string(max_text_size + 1, 'x')});
    });
    validate_request({"bob", ContentKind::text, {}, "file:///etc/passwd"});
    rejects([&] { validate_request({"bob", ContentKind::file, root, {}}); });
    std::filesystem::create_symlink(root / "sample.txt", root / "link.txt");
    rejects([&] { validate_request({"bob", ContentKind::file, root / "link.txt", {}}); });
    rejects([&] { Service invalid({root, 0, false}); });

    const auto left = root / "left", right = root / "right", output = root / "received";
    std::filesystem::create_directory(output);
    DeviceIdentity left_identity, right_identity;
    {
        IdentityStore store(left, true);
        left_identity = store.initialize();
    }
    {
        IdentityStore store(right, true);
        right_identity = store.initialize();
    }
    std::ofstream(root / "left.pem") << left_identity.certificate_pem;
    std::ofstream(root / "right.pem") << right_identity.certificate_pem;
    Socket reserved;
    const auto port = reserved.port;
    reserved.release();
    {
        IdentityStore store(left);
        store.pair("bob", root / "right.pem", right_identity.fingerprint,
                   "127.0.0.1:" + std::to_string(port));
    }
    {
        IdentityStore store(right);
        store.pair("alice", root / "left.pem", left_identity.fingerprint);
    }
    Service sender({left, 4269, false, std::chrono::seconds(3)});
    Service receiver(
        {right, port, live_discovery, std::chrono::milliseconds(live_discovery ? 5000 : 600)});
    Mailbox sent{sender, {}}, received{receiver, {}};
    receiver.start_receiving(output);
    received.await(Direction::receive, EventKind::listening);
    rejects([&] { receiver.start_receiving(output); });
#ifdef BEAM_HAS_DISCOVERY
    if (live_discovery) {
        DiscoveryOptions browse;
        browse.timeout = std::chrono::seconds(1);
        const auto devices = discover_devices(browse);
        require(std::any_of(devices.begin(), devices.end(),
                            [&](const auto& device) {
                                return std::any_of(
                                    device.endpoints.begin(), device.endpoints.end(),
                                    [&](const auto& endpoint) { return endpoint.port == port; });
                            }),
                "desktop listener was not advertised");
    } else
#endif
    {
        // Idle timeouts restart quietly rather than flooding failure notifications.
        received.await(Direction::receive, EventKind::listening);
        require(std::none_of(received.pending.begin(), received.pending.end(),
                             [](const Event& event) { return event.kind == EventKind::failed; }),
                "idle receiver produced a failure");
    }
    sender.check_peer("bob");
    require(sent.await(Direction::check, EventKind::completed).message.find("reachable") !=
                std::string::npos,
            "paired connectivity check failed");
    received.await(Direction::receive, EventKind::listening);
    require(std::filesystem::is_empty(output), "probe wrote received content");
    sender.check_peer("missing");
    require(sent.await(Direction::check, EventKind::failed).message.find("unavailable") !=
                std::string::npos,
            "unknown peer was reachable");
    for (const auto& request :
         std::vector<SendRequest>{{"bob", ContentKind::file, root / "sample.txt", {}},
                                  {"bob", ContentKind::text, {}, "hello\r\n世界"},
                                  {"bob", ContentKind::text, {}, "https://example.com/a"}}) {
        sender.send(request);
        const auto send_result = sent.await(Direction::send, EventKind::completed);
        const auto receive_result = received.await(Direction::receive, EventKind::completed);
        require(send_result.result.kind == request.kind &&
                    receive_result.result.kind == request.kind,
                "desktop content kind changed");
        const auto expected =
            request.kind == ContentKind::file ? read(request.file) : request.content;
        require((request.kind == ContentKind::file ? read(output / receive_result.result.filename)
                                                   : receive_result.result.text) == expected,
                "desktop content bytes changed");
        require(send_result.result.bytes == expected.size(),
                "desktop completed byte count changed");
        received.await(Direction::receive, EventKind::listening);
        require(std::none_of(received.pending.begin(), received.pending.end(),
                             [](const Event& event) {
                                 return event.direction == Direction::receive &&
                                        event.kind == EventKind::progress &&
                                        event.progress.stage == TransferStage::waiting &&
                                        event.progress.filename.empty();
                             }),
                "idle listener restart replaced completed transfer progress");
    }
    sender.send({"bob", ContentKind::file, root / "sample.txt", {}});
    sent.await(Direction::send, EventKind::failed);
    received.await(Direction::receive, EventKind::failed);
    require(read(output / "sample.txt") == "desktop file bytes\n",
            "failed duplicate overwrote output");
    received.await(Direction::receive, EventKind::listening);
    sender.send({"bob", ContentKind::text, {}, "after rejection"});
    sent.await(Direction::send, EventKind::completed);
    received.await(Direction::receive, EventKind::completed);
    rejects([&] { receiver.unpair("missing"); });
    require(receiver.is_receiving(), "invalid unpair stopped reception");
    receiver.unpair("alice");
    require(!receiver.is_receiving() && IdentityStore(right).peers().empty(),
            "unpair retained a listener or trusted peer");
    received.await(Direction::receive, EventKind::stopped);
#ifdef BEAM_HAS_DISCOVERY
    if (live_discovery) {
        DiscoveryOptions browse;
        browse.timeout = std::chrono::seconds(1);
        // mDNS goodbye records have a one-second cache grace period (RFC 6762
        // section 10.1). A single one-second browse races Avahi's withdrawal.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool withdrawn = false;
        while (!withdrawn && std::chrono::steady_clock::now() < deadline) {
            withdrawn = true;
            for (const auto& device : discover_devices(browse))
                for (const auto& endpoint : device.endpoints)
                    withdrawn &= endpoint.port != port;
        }
        require(withdrawn,
                "stopped desktop receiver is still advertised after withdrawal deadline");
    }
#endif
    // Empty trust fails before binding and the next start loads the changed store.
    receiver.start_receiving(output);
    received.await(Direction::receive, EventKind::failed);
    received.await(Direction::receive, EventKind::stopped);

    Socket blackhole;
    {
        IdentityStore store(left);
        store.set_endpoint("bob", "127.0.0.1:" + std::to_string(blackhole.port));
    }
    sender.send({"bob", ContentKind::text, {}, "cancel me"});
    rejects([&] { sender.send({"bob", ContentKind::text, {}, "second send"}); });
    sender.cancel_send();
    sent.await(Direction::send, EventKind::failed);
    const auto start = std::chrono::steady_clock::now();
    {
        Service closing({left, 4269, false});
        closing.send({"bob", ContentKind::text, {}, "close during connect"});
    }
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
            "shutdown did not cancel/join sender");
    sender.send({"bob", ContentKind::text, {}, "revoke during connect"});
    sender.check_peer("bob");
    const auto revoke_started = std::chrono::steady_clock::now();
    sender.unpair("bob");
    require(!sender.is_sending() && !sender.is_checking() && IdentityStore(left).peers().empty(),
            "unpair retained outgoing workers or trust");
    require(std::chrono::steady_clock::now() - revoke_started < std::chrono::seconds(2),
            "unpair did not cancel/join outgoing workers");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--discovery"),
                "expected optional --discovery");
        tests(argc == 2);
        std::cout << "desktop controller tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
