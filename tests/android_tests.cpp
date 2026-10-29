#include "android/session.hpp"
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
using beam::android::Command;
using beam::android::Session;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <typename Action> void rejects(Action action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Android operation accepted");
}
struct Temporary {
    std::filesystem::path root;
    Temporary() {
        auto pattern = (std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                        "beam-android-XXXXXX")
                           .string();
        const auto created = mkdtemp(pattern.data());
        require(created != nullptr, "Cannot create Android fixture");
        root = created;
    }
    ~Temporary() { std::filesystem::remove_all(root); }
};
std::uint16_t port() {
    const auto fd = socket(AF_INET, SOCK_DGRAM, 0);
    require(fd >= 0, "Cannot create test socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const auto bound = bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    socklen_t length = sizeof(address);
    const auto named = getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
    close(fd);
    require(bound == 0 && named == 0, "Cannot reserve Android test port");
    return ntohs(address.sin_port);
}
std::vector<std::string> await(Session& session, int direction, int kind) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto events = session.call(Command::events, {});
        require(events.size() % 11 == 0, "Malformed Android event rows");
        for (std::size_t i = 0; i < events.size(); i += 11) {
            if (events[i] == std::to_string(direction) && events[i + 1] == std::to_string(kind))
                return {events.begin() + static_cast<std::ptrdiff_t>(i),
                        events.begin() + static_cast<std::ptrdiff_t>(i + 11)};
            if (events[i + 1] == "3")
                throw std::runtime_error(events[i + 2]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Android session event timed out");
}
void idle(Session& session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (session.call(Command::busy, {}) != std::vector<std::string>{"0", "0", "0"}) {
        require(std::chrono::steady_clock::now() < deadline, "Android worker did not stop");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void tests() {
    Temporary fixture;
    const auto receiver_port = port();
    Session android(fixture.root / "android", receiver_port), desktop(fixture.root / "desktop");
    // Removed command IDs stay invalid; later JNI commands keep their values.
    static_assert(static_cast<int>(Command::pairing_approve) == 15);
    static_assert(static_cast<int>(Command::pairing_cancel) == 18);
    static_assert(static_cast<int>(Command::import_devices) == 25);
    rejects([&] { android.call(static_cast<Command>(14), {}); });
    rejects([&] { android.call(static_cast<Command>(17), {}); });
    const auto phone = android.call(Command::identity, {}),
               laptop = desktop.call(Command::identity, {});
    const auto phone_cert = fixture.root / "phone.pem", laptop_cert = fixture.root / "laptop.pem";
    std::ofstream(phone_cert) << phone[2];
    std::ofstream(laptop_cert) << laptop[2];
    rejects([&] { android.call(Command::pair, {"laptop", laptop_cert.string(), phone[0], ""}); });
    rejects(
        [&] { android.call(Command::pair, {"bad/name", laptop_cert.string(), laptop[0], ""}); });
    android.call(Command::pair, {"laptop", laptop_cert.string(), laptop[0], "127.0.0.1:4269"});
    desktop.call(Command::pair, {"phone", phone_cert.string(), phone[0],
                                 "127.0.0.1:" + std::to_string(receiver_port)});
    require(android.call(Command::peers, {})[2] == laptop[0], "Android trust not retained");
    const auto backup = android.call(Command::export_devices, {})[0];
    require(android.call(Command::preview_devices, {backup})[2] == "1",
            "Android backup preview failed");
    android.call(Command::edit_device,
                 {"laptop", "work-mac", "[::1]:5000", "100.64.0.1:5000", "", "", laptop[0]});
    require(android.call(Command::device_details, {})[0] == "work-mac", "Android rename failed");
    rejects([&] { android.call(Command::import_devices, {backup}); });
    android.call(Command::unpair, {"work-mac"});
    require(android.call(Command::import_devices, {backup})[1] == "1",
            "Android backup restore failed");
    rejects([&] { android.call(Command::send, {"laptop", "unknown", "", ""}); });
    rejects([&] { android.call(Command::send, {"laptop", "text", "", std::string(65537, 'a')}); });
    rejects([&] { android.call(Command::send, {"laptop", "text", "", std::string("a\0b", 3)}); });
    rejects([&] { android.call(Command::identity, {"extra"}); });
    rejects([&] { android.call(static_cast<Command>(99), {}); });
    rejects([&] { android.call(Command::send, {"laptop", "url", "", "file:///private"}); });
    const auto output = fixture.root / "received";
    std::filesystem::create_directory(output);
    for (const auto& kind : {"text", "file"}) {
        const std::string content =
            kind == std::string("url") ? "https://example.com/a%20b" : "hello 🌍\n世界";
        const auto source = fixture.root / "source.txt";
        std::ofstream(source) << content;
        android.call(Command::receive, {output.string()});
        await(android, 1, 0);
        rejects([&] { android.call(Command::endpoint, {"laptop", "127.0.0.1:4269"}); });
        desktop.call(Command::send,
                     {"phone", kind, kind == std::string("file") ? source.string() : "",
                      kind == std::string("file") ? "" : content});
        const auto event = await(android, 1, 2);
        await(desktop, 0, 2);
        require((kind == std::string("file") ? read(output / event[3]) : event[10]) == content,
                "Android received bytes changed");
        android.call(Command::stop, {});
        idle(android);
        idle(desktop);
        android.call(Command::events, {});
    }
    android.call(Command::receive, {output.string()});
    await(android, 1, 0);
    rejects([&] { android.call(Command::unpair, {"missing"}); });
    require(android.call(Command::busy, {})[1] == "1", "invalid unpair stopped reception");
    android.call(Command::unpair, {"laptop"});
    require(android.call(Command::busy, {}) == std::vector<std::string>{"0", "0", "0"},
            "Android unpair retained a worker using revoked trust");
    require(android.call(Command::peers, {}).empty(), "Android revocation failed");
    Session reopened(fixture.root / "android");
    require(reopened.call(Command::identity, {}) == phone,
            "Android identity changed after restart");
}
} // namespace
int main() {
    try {
        tests();
        std::cout << "Android session tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
