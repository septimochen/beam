#include "beam/tailscale.hpp"
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void require(bool condition, const char* label) {
    if (!condition)
        throw std::logic_error(label);
}
template <typename Action> void rejects(Action action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::logic_error("invalid Tailscale status accepted");
}
const std::string status =
    R"({"BackendState":"Running","Self":{"TailscaleIPs":["100.64.0.1"]},"Peer":{
"key1":{"DNSName":"macbook.test.ts.net.","Online":true,"TailscaleIPs":["100.64.0.2","fd7a:115c:a1e0::2","100.64.0.2"],"CurAddr":"1.2.3.4:12345","AllowedIPs":["10.0.0.0/8"]},
"key2":{"HostName":"offline","Online":false,"TailscaleIPs":["100.64.0.3"]},
"key3":{"HostName":"malformed","Online":true,"TailscaleIPs":["10.0.0.1","fd00::1","hostname"]},
"key4":{"DNSName":"bad\u001bname","Online":true,"TailscaleIPs":["100.64.0.4"]},
"key5":{"DNSName":"shape","Online":"yes","TailscaleIPs":["100.64.0.5"]}}})";
struct Workspace {
    std::filesystem::path path;
    Workspace() {
        char pattern[] = "/tmp/beam-tailscale-XXXXXX";
        const auto* root = mkdtemp(pattern);
        if (!root)
            throw std::runtime_error("create fixture directory failed");
        path = root;
    }
    ~Workspace() { std::filesystem::remove_all(path); }
};
std::string script(const Workspace& workspace, const std::string& body) {
    const auto path = workspace.path / "fake tailscale";
    std::ofstream file(path);
    file << "#!/bin/sh\n[ \"$#\" -eq 2 ] && [ \"$1\" = status ] && [ \"$2\" = --json ] || exit 8\n"
         << body;
    file.close();
    if (chmod(path.c_str(), 0700) != 0)
        throw std::runtime_error("prepare fixture failed");
    return path.string();
}
} // namespace
int main() {
    try {
        const auto devices = beam::parse_tailscale_status(status, 5270);
        require(devices.size() == 2 && devices[0].endpoints.size() == 2 && devices[0].online &&
                    devices[0].endpoints[0].port == 5270 && !devices[1].online,
                "wrong peer filtering or port");
        require(beam::parse_tailscale_status(R"({"BackendState":"Running","Peer":null})").empty(),
                "empty tailnet failed");
        for (const auto* json :
             {"", "{}", "[]", "garbage", R"({"BackendState":"NeedsLogin","AuthURL":"secret"})",
              R"({"BackendState":"Running","Peer":[]})",
              R"({"BackendState":"Running","BackendState":"Running"})"})
            rejects([&] { static_cast<void>(beam::parse_tailscale_status(json)); });
        rejects([&] { static_cast<void>(beam::parse_tailscale_status(status, 0)); });
        rejects([&] {
            static_cast<void>(beam::parse_tailscale_status(std::string(1024 * 1024 + 1, 'x')));
        });
        rejects([&] {
            static_cast<void>(beam::parse_tailscale_status(
                "{\"BackendState\":\"Running\",\"extra\":" + std::string(32, '[') + "0" +
                std::string(32, ']') + "}"));
        });
        std::string many = "{\"BackendState\":\"Running\",\"Peer\":{";
        for (int index = 0; index < 513; ++index)
            many += (index ? "," : "") + std::string("\"") + std::to_string(index) + "\":{}";
        rejects([&] { static_cast<void>(beam::parse_tailscale_status(many + "}}")); });
        Workspace workspace;
        beam::DiscoveryOptions options;
        options.timeout = std::chrono::seconds(2);
        auto executable = script(workspace, "cat <<'JSON'\n" + status + "\nJSON\n");
        require(beam::discover_tailscale(options, 4269, executable).size() == 2,
                "process discovery failed");
        executable = script(workspace, "echo secret >&2\nexit 1\n");
        rejects([&] { static_cast<void>(beam::discover_tailscale(options, 4269, executable)); });
        rejects([&] {
            static_cast<void>(
                beam::discover_tailscale(options, 4269, (workspace.path / "missing").string()));
        });
        executable = script(workspace, "printf not-json\n");
        rejects([&] { static_cast<void>(beam::discover_tailscale(options, 4269, executable)); });
        executable =
            script(workspace, "while :; do printf 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'; done\n");
        rejects([&] { static_cast<void>(beam::discover_tailscale(options, 4269, executable)); });
        // A descendant holds stdout even after the CLI exits. The process group
        // must be killed and the direct child reaped at the total deadline.
        executable = script(workspace, "sleep 30 &\nexit 0\n");
        options.timeout = std::chrono::milliseconds(100);
        const auto start = std::chrono::steady_clock::now();
        rejects([&] { static_cast<void>(beam::discover_tailscale(options, 4269, executable)); });
        require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
                "child timeout unbounded");
        options.timeout = std::chrono::seconds(2);
        options.should_cancel = [deadline = std::chrono::steady_clock::now() +
                                            std::chrono::milliseconds(50)] {
            return std::chrono::steady_clock::now() >= deadline;
        };
        try {
            static_cast<void>(beam::discover_tailscale(options, 4269, executable));
            throw std::logic_error("cancelled CLI succeeded");
        } catch (const beam::DiscoveryError& error) {
            require(error.cancelled, "wrong cancellation category");
        }
        options.should_cancel = [calls = 0]() mutable -> bool {
            if (++calls > 2)
                throw std::logic_error("cancel callback");
            return false;
        };
        rejects([&] { static_cast<void>(beam::discover_tailscale(options, 4269, executable)); });
        int child_status = 0;
        require(waitpid(-1, &child_status, WNOHANG) == -1 && errno == ECHILD,
                "discovery left an unreaped child");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
