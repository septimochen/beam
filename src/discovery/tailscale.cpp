#include "beam/tailscale.hpp"
#include "beam/routing.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <set>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <tuple>
#include <unistd.h>

extern char** environ;

namespace beam {
namespace {
constexpr std::size_t max_status_size = 1024 * 1024;
constexpr std::size_t max_peers = 512;
void checked(bool success, const char* message) {
    if (!success)
        throw DiscoveryError(message);
}
bool safe_name(std::string_view name) {
    return !name.empty() && name.size() <= 253 &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) { return c > 32 && c < 127; });
}
struct File {
    int value;
    explicit File(int descriptor) : value(descriptor) {}
    ~File() {
        if (value >= 0)
            close(value);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};
struct Actions {
    posix_spawn_file_actions_t value{};
    Actions() {
        checked(posix_spawn_file_actions_init(&value) == 0, "initialize Tailscale process failed");
    }
    ~Actions() { posix_spawn_file_actions_destroy(&value); }
    Actions(const Actions&) = delete;
    Actions& operator=(const Actions&) = delete;
};
struct Attributes {
    posix_spawnattr_t value{};
    Attributes() {
        checked(posix_spawnattr_init(&value) == 0, "initialize Tailscale process failed");
    }
    ~Attributes() { posix_spawnattr_destroy(&value); }
    Attributes(const Attributes&) = delete;
    Attributes& operator=(const Attributes&) = delete;
};
struct Child {
    pid_t value{-1};
    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() {
        if (value > 0) {
            // Own a process group, including descendants that might retain the pipe.
            kill(-value, SIGKILL);
            int status = 0;
            while (waitpid(value, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
};
std::string status_output(const DiscoveryOptions& options, const std::string& executable) {
    if (options.timeout <= std::chrono::milliseconds::zero() ||
        options.timeout > std::chrono::minutes(1))
        throw std::invalid_argument(
            "Tailscale discovery timeout must be positive and at most 60 seconds");
    if (executable.empty() || executable.size() > 4096 ||
        executable.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid Tailscale executable path");
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    const auto check = [&] {
        if (options.stop_token.stop_requested() ||
            (options.should_cancel && options.should_cancel()))
            throw DiscoveryError("Tailscale discovery cancelled", true);
        if (std::chrono::steady_clock::now() >= deadline)
            throw DiscoveryError("Tailscale discovery timed out");
    };
    check();
    std::array<int, 2> descriptors{};
    checked(pipe(descriptors.data()) == 0, "create Tailscale output pipe failed");
    File reader(descriptors[0]), writer(descriptors[1]);
    // Avoid collisions with standard descriptors when a caller has closed stdio.
    File input(fcntl(reader.value, F_DUPFD_CLOEXEC, 3));
    File output(fcntl(writer.value, F_DUPFD_CLOEXEC, 3));
    checked(input.value >= 0 && output.value >= 0, "prepare Tailscale output pipe failed");
    close(reader.value);
    reader.value = -1;
    close(writer.value);
    writer.value = -1;
    checked(fcntl(input.value, F_SETFL, O_NONBLOCK) == 0, "prepare Tailscale output pipe failed");
    Actions actions;
    checked(posix_spawn_file_actions_adddup2(&actions.value, output.value, STDOUT_FILENO) == 0 &&
                posix_spawn_file_actions_addclose(&actions.value, input.value) == 0 &&
                posix_spawn_file_actions_addclose(&actions.value, output.value) == 0 &&
                posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO, "/dev/null",
                                                 O_RDONLY, 0) == 0 &&
                posix_spawn_file_actions_addopen(&actions.value, STDERR_FILENO, "/dev/null",
                                                 O_WRONLY, 0) == 0,
            "configure Tailscale process failed");
    Attributes attributes;
    checked(posix_spawnattr_setpgroup(&attributes.value, 0) == 0 &&
                posix_spawnattr_setflags(&attributes.value, POSIX_SPAWN_SETPGROUP) == 0,
            "configure Tailscale process group failed");
    std::array<char*, 4> arguments{const_cast<char*>(executable.c_str()),
                                   const_cast<char*>("status"), const_cast<char*>("--json"),
                                   nullptr};
    Child child;
    pid_t spawned = -1;
    checked(posix_spawnp(&spawned, executable.c_str(), &actions.value, &attributes.value,
                         arguments.data(), environ) == 0,
            "cannot run tailscale; install its CLI or use --tailscale-cli PATH");
    child.value = spawned;
    close(output.value);
    output.value = -1;
    std::string result;
    std::array<char, 8192> buffer{};
    for (;;) {
        check();
        const auto count = read(input.value, buffer.data(), buffer.size());
        if (count == 0)
            break;
        if (count > 0) {
            checked(static_cast<std::size_t>(count) <= max_status_size - result.size(),
                    "Tailscale status output exceeds 1 MiB");
            result.append(buffer.data(), static_cast<std::size_t>(count));
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw DiscoveryError("read Tailscale status failed");
        } else {
            pollfd descriptor{input.value, POLLIN, 0};
            const int ready = poll(&descriptor, 1, 25);
            checked(ready >= 0 || errno == EINTR, "poll Tailscale status failed");
            checked(!(descriptor.revents & (POLLERR | POLLNVAL)), "Tailscale output pipe failed");
        }
    }
    for (;;) {
        check();
        int status = 0;
        const auto completed = waitpid(child.value, &status, WNOHANG);
        if (completed == child.value) {
            kill(-child.value, SIGKILL);
            child.value = -1;
            checked(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                    "tailscale status failed; check the local Tailscale daemon and login");
            return result;
        }
        checked(completed == 0 || errno == EINTR, "reap Tailscale process failed");
        poll(nullptr, 0, 25);
    }
}
} // namespace
std::vector<TailscaleDevice> parse_tailscale_status(std::string_view text, std::uint16_t port) {
    if (!port)
        throw std::invalid_argument("Tailscale port must be between 1 and 65535");
    checked(!text.empty() && text.size() <= max_status_size, "invalid Tailscale status size");
    using Json = nlohmann::json;
    Json status;
    try {
        // Bound recursion before the DOM parser descends and reject duplicate keys.
        std::array<std::set<std::string>, 17> keys;
        status = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
            checked(depth >= 0 && depth < 16, "Tailscale status nesting exceeds limit");
            const auto index = static_cast<std::size_t>(depth);
            if (event == Json::parse_event_t::object_start)
                keys[index].clear();
            if (event == Json::parse_event_t::key)
                checked(index > 0 && keys[index - 1].insert(value.get<std::string>()).second,
                        "duplicate Tailscale status key");
            return true;
        });
    } catch (const Json::exception&) {
        throw DiscoveryError("invalid Tailscale status JSON");
    }
    checked(status.is_object() && status.contains("BackendState") &&
                status["BackendState"] == "Running",
            "Tailscale is not running; connect it before discovering peers");
    if (!status.contains("Peer") || status["Peer"].is_null())
        return {};
    const auto& peers = status["Peer"];
    checked(peers.is_object() && peers.size() <= max_peers, "invalid Tailscale peer count");
    std::vector<TailscaleDevice> devices;
    for (const auto& peer : peers) {
        if (!peer.is_object() || !peer.contains("Online") || !peer["Online"].is_boolean() ||
            !peer.contains("TailscaleIPs") || !peer["TailscaleIPs"].is_array() ||
            peer["TailscaleIPs"].size() > 16)
            continue;
        std::string name;
        for (const auto field : {"DNSName", "HostName"})
            if (peer.contains(field) && peer[field].is_string() &&
                safe_name(peer[field].get<std::string>())) {
                name = peer[field].get<std::string>();
                break;
            }
        if (name.empty())
            continue;
        TailscaleDevice device{name, peer["Online"].get<bool>(), {}};
        for (const auto& ip : peer["TailscaleIPs"]) {
            if (!ip.is_string())
                continue;
            const auto host = ip.get<std::string>();
            if (!is_tailscale_address(host))
                continue;
            const auto endpoint = numeric_endpoint(endpoint_string({host, port}));
            if (std::none_of(device.endpoints.begin(), device.endpoints.end(),
                             [&](const auto& saved) {
                                 return endpoint_string(saved) == endpoint_string(endpoint);
                             }))
                device.endpoints.push_back(endpoint);
        }
        if (!device.endpoints.empty())
            devices.push_back(std::move(device));
    }
    std::sort(devices.begin(), devices.end(), [](const auto& left, const auto& right) {
        return std::tie(left.name, left.online) < std::tie(right.name, right.online);
    });
    return devices;
}
std::vector<TailscaleDevice> discover_tailscale(const DiscoveryOptions& options, std::uint16_t port,
                                                const std::string& executable) {
    if (!port)
        throw std::invalid_argument("Tailscale port must be between 1 and 65535");
    const auto start = std::chrono::steady_clock::now();
    auto devices = parse_tailscale_status(status_output(options, executable), port);
    if (options.stop_token.stop_requested() || (options.should_cancel && options.should_cancel()))
        throw DiscoveryError("Tailscale discovery cancelled", true);
    if (std::chrono::steady_clock::now() - start >= options.timeout)
        throw DiscoveryError("Tailscale discovery timed out");
    return devices;
}
} // namespace beam
