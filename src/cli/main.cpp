#include "beam/beam.hpp"
#ifdef BEAM_HAS_DISCOVERY
#include "beam/discovery.hpp"
#endif
#ifdef BEAM_HAS_QUIC
#include "beam/transfer.hpp"
#endif
#include <charconv>
#include <csignal>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void print_help() {
    std::cout
        << "Beam — direct file transfer over QUIC\n\n"
           "Usage:\n"
           "  beam --help\n"
           "  beam --version\n"
           "  beam devices [--timeout SECONDS]\n"
           "  beam receive --listen PORT --output DIR --cert PEM --key PEM --ca PEM "
           "[--timeout SECONDS] [--no-progress] [--name NAME] [--no-discovery]\n"
           "  beam send IP:PORT FILE --cert PEM --key PEM --ca PEM [--server-name NAME] "
           "[--timeout SECONDS] [--no-progress]\n\n"
           "Receive one file from one authenticated peer, then exit. DIR must exist.\n"
           "Both peers need certificates issued by the private CA supplied in --ca.\n"
           "The server certificate must match IP or --server-name. IPv6: [ADDRESS]:PORT.\n"
           "Progress goes to stderr; --no-progress disables it. Ctrl-C cancels cleanly.\n"
           "Receivers advertise on LAN; --no-discovery disables this. Discovery is not trust.\n"
           "Timeout defaults to 30 seconds per wait. Existing files are never overwritten.\n";
}
#if defined(BEAM_HAS_QUIC) || defined(BEAM_HAS_DISCOVERY)
volatile std::sig_atomic_t interrupted = 0;
void interrupt_handler(int) { interrupted = 1; }
class InterruptGuard {
    struct sigaction previous{};

  public:
    InterruptGuard() {
        interrupted = 0;
        struct sigaction action{};
        action.sa_handler = interrupt_handler;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, &previous) != 0)
            throw std::runtime_error("install Ctrl-C handler failed");
    }
    ~InterruptGuard() { sigaction(SIGINT, &previous, nullptr); }
};
#ifdef BEAM_HAS_QUIC
const char* stage_name(beam::TransferStage stage) {
    switch (stage) {
    case beam::TransferStage::hashing:
        return "Hashing";
    case beam::TransferStage::connecting:
        return "Connecting";
    case beam::TransferStage::waiting:
        return "Waiting";
    case beam::TransferStage::transferring:
        return "Transferring";
    case beam::TransferStage::verifying:
        return "Verifying";
    case beam::TransferStage::complete:
        return "Complete";
    }
    return "Unknown";
}
#endif
unsigned number(const std::string& text, unsigned maximum, const char* label) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > maximum)
        throw std::invalid_argument(std::string("invalid ") + label);
    return value;
}
#ifdef BEAM_HAS_DISCOVERY
int devices_command(int argc, char** argv) {
    beam::DiscoveryOptions options;
    if (argc != 2) {
        if (argc != 4 || std::string_view(argv[2]) != "--timeout")
            throw std::invalid_argument("devices accepts only --timeout SECONDS");
        options.timeout = std::chrono::seconds(number(argv[3], 60, "discovery timeout"));
    }
    InterruptGuard interrupts;
    options.should_cancel = [] { return interrupted != 0; };
    const auto devices = beam::discover_devices(options);
    if (devices.empty()) {
        std::cout << "No Beam receivers found on the LAN.\n";
        return 0;
    }
    std::cout << "NAME\tENDPOINT\tHOST\tINTERFACE\n";
    for (const auto& device : devices)
        for (const auto& endpoint : device.endpoints)
            std::cout << device.name << '\t'
                      << (endpoint.host.find(':') == std::string::npos ? endpoint.host
                                                                       : "[" + endpoint.host + "]")
                      << ':' << endpoint.port << '\t' << device.hostname << '\t'
                      << device.interface_index << '\n';
    std::cerr << "Discovered endpoints are untrusted; sending still requires certificates.\n";
    return 0;
}
#endif
#ifdef BEAM_HAS_QUIC
int transfer_command(int argc, char** argv, bool receive) {
    std::map<std::string, std::string> options;
    std::vector<std::string> positional;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (!argument.starts_with("--")) {
            positional.push_back(argument);
            continue;
        }
        if (argument == "--no-progress" || (receive && argument == "--no-discovery")) {
            if (!options.emplace(argument, "").second)
                throw std::invalid_argument("duplicate option: " + argument);
            continue;
        }
        if (argument != "--cert" && argument != "--key" && argument != "--ca" &&
            argument != "--timeout" &&
            !(receive &&
              (argument == "--listen" || argument == "--output" || argument == "--name")) &&
            !(!receive && argument == "--server-name"))
            throw std::invalid_argument("unknown option: " + argument);
        if (++index == argc || std::string_view{argv[index]}.starts_with("--"))
            throw std::invalid_argument("missing value for " + argument);
        if (!options.emplace(argument, argv[index]).second)
            throw std::invalid_argument("duplicate option: " + argument);
    }
    const auto required = [&](const char* name) -> std::string {
        const auto found = options.find(name);
        if (found == options.end() || found->second.empty())
            throw std::invalid_argument(std::string("missing required option ") + name);
        return found->second;
    };
    beam::TransferOptions configuration{{required("--cert"), required("--key"), required("--ca")}};
#ifdef BEAM_HAS_DISCOVERY
    if (options.contains("--name") && !beam::is_discovery_name(options.at("--name")))
        throw std::invalid_argument("name must be 1-63 printable ASCII bytes without outer spaces");
#else
    if (options.contains("--name"))
        throw std::invalid_argument("LAN discovery is disabled in this build");
#endif
    if (options.contains("--name") && options.contains("--no-discovery"))
        throw std::invalid_argument("--name cannot be combined with --no-discovery");
    if (options.contains("--timeout"))
        configuration.timeout =
            std::chrono::seconds(number(options.at("--timeout"), 86400, "timeout"));
    InterruptGuard interrupts;
    configuration.should_cancel = [] { return interrupted != 0; };
    auto last_update = std::chrono::steady_clock::time_point{};
    auto last_stage = beam::TransferStage::complete;
    if (!options.contains("--no-progress")) {
        configuration.on_progress = [&](const beam::TransferProgress& update) {
            const auto now = std::chrono::steady_clock::now();
            // The final chunk may precede verification; only Complete denotes success.
            if (update.stage == last_stage && now - last_update < std::chrono::milliseconds(250))
                return;
            last_stage = update.stage;
            last_update = now;
            std::cerr << stage_name(update.stage);
            if (!update.filename.empty())
                std::cerr << " " << update.filename;
            if (update.stage == beam::TransferStage::hashing ||
                update.stage == beam::TransferStage::transferring ||
                update.stage == beam::TransferStage::complete)
                std::cerr << ": " << update.bytes << "/" << update.total << " bytes";
            std::cerr << '\n';
        };
    }
    beam::TransferResult result;
    if (receive) {
        if (!positional.empty())
            throw std::invalid_argument("receive takes no positional arguments");
        const auto port =
            static_cast<std::uint16_t>(number(required("--listen"), 65535, "listen port"));
        const auto output = required("--output");
#ifdef BEAM_HAS_DISCOVERY
        std::unique_ptr<beam::ReceiverAdvertisement> advertisement;
        configuration.should_cancel = [&] {
            if (advertisement) {
                try {
                    advertisement->check();
                } catch (const std::exception& error) {
                    std::cerr << "beam: LAN advertisement stopped: " << error.what() << '\n';
                    advertisement.reset();
                }
            }
            return interrupted != 0;
        };
#endif
        configuration.on_listening = [&] {
#ifdef BEAM_HAS_DISCOVERY
            if (!options.contains("--no-discovery")) {
                beam::DiscoveryOptions discovery;
                discovery.should_cancel = [] { return interrupted != 0; };
                try {
                    advertisement = std::make_unique<beam::ReceiverAdvertisement>(
                        port, options.contains("--name") ? options.at("--name") : "Beam",
                        discovery);
                    std::cerr << "LAN receiver name: " << advertisement->name() << '\n';
                } catch (const beam::DiscoveryError& error) {
                    if (error.cancelled)
                        throw;
                    std::cerr << "beam: LAN advertisement unavailable: " << error.what()
                              << "; direct transfer remains available\n";
                }
            }
#endif
            std::cout << "Listening for one authenticated peer on UDP port " << port << "..."
                      << std::endl;
        };
        result = beam::receive_file(port, output, configuration);
        std::cout << "Received and verified ";
    } else {
        if (positional.size() != 2)
            throw std::invalid_argument("send requires IP:PORT and FILE");
        beam::Endpoint endpoint;
        try {
            endpoint = beam::parse_endpoint(positional[0]);
        } catch (const std::exception& error) {
            throw std::invalid_argument(error.what());
        }
        result = beam::send_file(endpoint, positional[1],
                                 options.contains("--server-name") ? options.at("--server-name")
                                                                   : endpoint.host,
                                 configuration);
        std::cout << "Sent and verified by peer ";
    }
    std::cout << result.filename << " (" << result.bytes << " bytes, SHA-256)\n";
    return 0;
}
#endif
#endif
} // namespace
int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        print_help();
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "Beam " << beam::version() << '\n';
        return 0;
    }
    const std::string_view command{argv[1]};
    try {
        if (command == "devices") {
#ifdef BEAM_HAS_DISCOVERY
            return devices_command(argc, argv);
#else
            throw std::runtime_error("LAN discovery is disabled in this build; see README.md");
#endif
        }
        if (command == "send" || command == "receive") {
#ifdef BEAM_HAS_QUIC
            return transfer_command(argc, argv, command == "receive");
#else
            throw std::runtime_error(
                "QUIC transfer is available in macOS/Linux builds; see README.md");
#endif
        }
        throw std::invalid_argument("Unsupported command or arguments. Run beam --help.");
    } catch (const std::invalid_argument& error) {
        std::cerr << "beam: " << error.what() << '\n';
        return 2;
#ifdef BEAM_HAS_QUIC
    } catch (const beam::TransferError& error) {
        std::cerr << "beam [" << beam::error_name(error.code) << "]: " << error.what() << '\n';
        return error.code == beam::TransferErrorCode::cancelled ? 130 : 1;
#endif
#ifdef BEAM_HAS_DISCOVERY
    } catch (const beam::DiscoveryError& error) {
        std::cerr << "beam: " << error.what() << '\n';
        return error.cancelled ? 130 : 1;
#endif
    } catch (const std::exception& error) {
        std::cerr << "beam: " << error.what() << '\n';
        return 1;
    }
}
