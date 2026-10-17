#include "beam/beam.hpp"
#include "beam/config.hpp"
#ifdef BEAM_HAS_DISCOVERY
#include "beam/discovery.hpp"
#endif
#ifdef BEAM_HAS_QUIC
#include "beam/identity.hpp"
#include "beam/routing.hpp"
#include "beam/transfer.hpp"
#endif
#ifdef BEAM_HAS_TAILSCALE
#include "beam/tailscale.hpp"
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
        << "Beam — direct file and text transfer over QUIC\n\n"
           "Usage:\n"
           "  beam --help\n"
           "  beam --version\n"
           "  beam identity init|show|export [--state-dir DIR]\n"
           "  beam pair NAME --cert PEM --fingerprint SHA256 [--endpoint IP:PORT] [--state-dir "
           "DIR]\n"
           "  beam peers [--state-dir DIR]\n"
           "  beam unpair NAME [--state-dir DIR]\n"
           "  beam peer NAME --endpoint IP:PORT [--state-dir DIR]\n"
           "  beam peer NAME --tailscale-endpoint IP:PORT [--state-dir DIR]\n"
           "  beam peer NAME --clear-tailscale [--state-dir DIR]\n"
           "  beam devices --tailscale [--port PORT] [--timeout SECONDS] [--tailscale-cli PATH]\n"
           "  beam send FILE PEER [--endpoint IP:PORT] [--state-dir DIR] [--timeout SECONDS] "
           "[--no-progress]\n"
           "  beam text TEXT PEER [--endpoint IP:PORT] [--state-dir DIR] [--timeout SECONDS] "
           "[--no-progress]\n"
           "  beam check PEER [--endpoint IP:PORT] [--state-dir DIR] [--timeout SECONDS]\n"
           "  beam receive --output DIR [--listen PORT] [--state-dir DIR] [--name NAME] "
           "[--no-discovery]\n"
           "  beam devices [--timeout SECONDS]\n"
           "  beam receive --listen PORT --output DIR --cert PEM --key PEM --ca PEM "
           "[--timeout SECONDS] [--no-progress] [--name NAME] [--no-discovery]\n"
           "  beam send IP:PORT FILE --cert PEM --key PEM --ca PEM [--server-name NAME] "
           "[--timeout SECONDS] [--no-progress]\n\n"
           "Receive one file or text from one authenticated peer, then exit. DIR must exist.\n"
           "Paired send/text/check accept --route auto|direct|tailscale; default: direct first.\n"
           "--endpoint overrides all saved routes. Fallback only precedes authentication.\n"
           "Received text is written verbatim to stdout; files require a receive folder.\n"
           "Text (including URLs): UTF-8, up to 64 KiB. Config: ~/.config/beam/config.\n"
           "Use -- before positional arguments that start with --.\n"
           "Initialize an identity and explicitly pair certificates with verified fingerprints.\n"
           "Legacy --cert/--key/--ca transfers still require a dedicated private CA.\n"
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
    std::cerr
        << "Discovered endpoints are untrusted; use a paired peer or verified CA credentials.\n";
    return 0;
}
#endif
#ifdef BEAM_HAS_TAILSCALE
int tailscale_devices_command(int argc, char** argv) {
    beam::DiscoveryOptions options;
    std::uint16_t port = 4269;
    std::string executable = "tailscale";
    std::map<std::string, std::string> arguments;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--tailscale") {
            if (!arguments.emplace(argument, "").second)
                throw std::invalid_argument("duplicate option: " + argument);
            continue;
        }
        if (argument != "--timeout" && argument != "--port" && argument != "--tailscale-cli")
            throw std::invalid_argument("unknown Tailscale discovery option: " + argument);
        if (++index == argc || std::string_view(argv[index]).starts_with("--"))
            throw std::invalid_argument("missing value for " + argument);
        if (!arguments.emplace(argument, argv[index]).second)
            throw std::invalid_argument("duplicate option: " + argument);
    }
    if (arguments.contains("--timeout"))
        options.timeout =
            std::chrono::seconds(number(arguments.at("--timeout"), 60, "discovery timeout"));
    if (arguments.contains("--port"))
        port = static_cast<std::uint16_t>(number(arguments.at("--port"), 65535, "Tailscale port"));
    if (arguments.contains("--tailscale-cli"))
        executable = arguments.at("--tailscale-cli");
    InterruptGuard interrupts;
    options.should_cancel = [] { return interrupted != 0; };
    const auto devices = beam::discover_tailscale(options, port, executable);
    std::cout << "NAME\tENDPOINT\tSTATUS\n";
    for (const auto& device : devices)
        for (const auto& endpoint : device.endpoints)
            std::cout << device.name << '\t' << beam::endpoint_string(endpoint) << '\t'
                      << (device.online ? "online" : "offline") << '\n';
    std::cerr << "Tailscale addresses are untrusted hints; Beam receiver availability and pairing "
                 "must be checked independently.\n";
    return 0;
}
#endif
#ifdef BEAM_HAS_QUIC
std::filesystem::path state_directory(const std::map<std::string, std::string>& options) {
    return options.contains("--state-dir") ? std::filesystem::path(options.at("--state-dir"))
                                           : beam::default_state_directory();
}
int identity_command(int argc, char** argv) {
    const std::string command = argv[1];
    std::map<std::string, std::string> options;
    std::vector<std::string> positional;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (!argument.starts_with("--")) {
            positional.push_back(argument);
            continue;
        }
        if (command == "peer" && argument == "--clear-tailscale") {
            if (!options.emplace(argument, "").second)
                throw std::invalid_argument("duplicate option: " + argument);
            continue;
        }
        if (argument != "--state-dir" &&
            !(command == "pair" &&
              (argument == "--cert" || argument == "--fingerprint" || argument == "--endpoint")) &&
            !(command == "peer" &&
              (argument == "--endpoint" || argument == "--tailscale-endpoint")))
            throw std::invalid_argument("unknown option: " + argument);
        if (++index == argc || std::string_view(argv[index]).starts_with("--"))
            throw std::invalid_argument("missing value for " + argument);
        if (!options.emplace(argument, argv[index]).second)
            throw std::invalid_argument("duplicate option: " + argument);
    }
    const auto required = [&](const char* name) -> std::string {
        if (!options.contains(name) || options.at(name).empty())
            throw std::invalid_argument(std::string("missing required option ") + name);
        return options.at(name);
    };
    if (command == "identity") {
        if (positional.size() != 1 ||
            (positional[0] != "init" && positional[0] != "show" && positional[0] != "export"))
            throw std::invalid_argument("identity requires init, show, or export");
        beam::IdentityStore store(state_directory(options), positional[0] == "init");
        const auto identity = positional[0] == "init" ? store.initialize() : store.identity();
        if (positional[0] == "export")
            std::cout << identity.certificate_pem;
        else
            std::cout << "Identity: " << identity.tls_name
                      << "\nFingerprint: " << identity.fingerprint << '\n';
        return 0;
    }
    if (command == "peers") {
        if (!positional.empty())
            throw std::invalid_argument("peers takes no positional arguments");
        beam::IdentityStore store(state_directory(options));
        std::cout << "NAME\tENDPOINT\tTAILSCALE\tFINGERPRINT\n";
        for (const auto& peer : store.peers())
            std::cout << peer.name << '\t' << (peer.endpoint.empty() ? "(unset)" : peer.endpoint)
                      << '\t'
                      << (peer.tailscale_endpoint.empty() ? "(unset)" : peer.tailscale_endpoint)
                      << '\t' << peer.identity.fingerprint << '\n';
        return 0;
    }
    if (positional.size() != 1 || !beam::is_peer_name(positional[0]))
        throw std::invalid_argument("command requires one lowercase peer name");
    if (command == "pair") {
        const auto certificate = required("--cert");
        const auto fingerprint = required("--fingerprint");
        beam::IdentityStore store(state_directory(options));
        const auto peer =
            store.pair(positional[0], certificate, fingerprint,
                       options.contains("--endpoint") ? options.at("--endpoint") : "");
        std::cout << "Paired " << peer.name << " (" << peer.identity.fingerprint << ")\n";
    } else if (command == "unpair") {
        beam::IdentityStore store(state_directory(options));
        store.unpair(positional[0]);
        std::cout << "Removed trusted peer " << positional[0] << '\n';
    } else {
        const auto choices = static_cast<unsigned>(options.contains("--endpoint")) +
                             static_cast<unsigned>(options.contains("--tailscale-endpoint")) +
                             static_cast<unsigned>(options.contains("--clear-tailscale"));
        if (choices == 0)
            throw std::invalid_argument(
                "missing required option --endpoint or --tailscale-endpoint");
        if (choices != 1)
            throw std::invalid_argument("choose one endpoint update at a time");
        beam::IdentityStore store(state_directory(options));
        if (options.contains("--endpoint"))
            store.set_endpoint(positional[0], required("--endpoint"));
        else
            store.set_tailscale_endpoint(positional[0], options.contains("--clear-tailscale")
                                                            ? ""
                                                            : required("--tailscale-endpoint"));
        std::cout << "Updated endpoint for " << positional[0] << '\n';
    }
    return 0;
}
int transfer_command(int argc, char** argv, bool receive) {
    const std::string command = argv[1];
    const bool typed = command == "text";
    const bool checking = command == "check";
    const auto defaults = beam::load_config(beam::default_config_file());
    std::map<std::string, std::string> options;
    std::vector<std::string> positional;
    bool positional_only = false;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (!positional_only && argument == "--") {
            positional_only = true;
            continue;
        }
        if (positional_only || !argument.starts_with("--")) {
            positional.push_back(argument);
            continue;
        }
        if (argument == "--no-progress" || (receive && argument == "--no-discovery")) {
            if (!options.emplace(argument, "").second)
                throw std::invalid_argument("duplicate option: " + argument);
            continue;
        }
        if ((typed || checking) && (argument == "--cert" || argument == "--key" ||
                                    argument == "--ca" || argument == "--server-name"))
            throw std::invalid_argument(
                "text/check commands use paired peers; omit manual credentials");
        if (argument != "--cert" && argument != "--key" && argument != "--ca" &&
            argument != "--timeout" && argument != "--state-dir" &&
            !(receive &&
              (argument == "--listen" || argument == "--output" || argument == "--name")) &&
            !(!receive &&
              (argument == "--server-name" || argument == "--endpoint" || argument == "--route")))
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
    const bool manual =
        options.contains("--cert") || options.contains("--key") || options.contains("--ca");
    if (checking) {
        if (positional.size() != 1)
            throw std::invalid_argument("check requires one paired PEER");
        positional.insert(positional.begin(), "");
    }
    beam::TransferOptions configuration{};
    configuration.timeout =
        checking ? std::min(defaults.timeout, std::chrono::milliseconds(5000)) : defaults.timeout;
    beam::Endpoint endpoint{};
    std::string filename, server_name;
    if (manual) {
        if (options.contains("--state-dir") || options.contains("--endpoint") ||
            options.contains("--route"))
            throw std::invalid_argument(
                "manual credentials cannot be combined with --state-dir, --endpoint or --route");
        configuration.credentials = {required("--cert"), required("--key"), required("--ca")};
    }
    if (receive) {
        if (!positional.empty())
            throw std::invalid_argument("receive takes no positional arguments");

        if (options.contains("--listen"))
            static_cast<void>(number(options.at("--listen"), 65535, "listen port"));
    } else {
        if (positional.size() != 2)
            throw std::invalid_argument(typed ? command + " requires CONTENT and PEER"
                                              : "send requires IP:PORT and FILE, or FILE and PEER");
        if (typed)
            beam::validate_content(beam::ContentKind::text, positional[0]);
        if (!typed && !checking && (manual || positional[0].find(':') != std::string::npos)) {
            if (!manual)
                static_cast<void>(required("--cert"));
            try {
                endpoint = beam::parse_endpoint(positional[0]);
            } catch (const std::exception& error) {
                throw std::invalid_argument(error.what());
            }
            filename = positional[1];
            server_name =
                options.contains("--server-name") ? options.at("--server-name") : endpoint.host;
        } else {
            if (options.contains("--server-name"))
                throw std::invalid_argument(
                    "paired sends use the saved TLS identity; omit --server-name");
            if (!beam::is_peer_name(positional[1]))
                throw std::invalid_argument("invalid peer name");
            filename = positional[0];
        }
    }
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
    if (checking)
        configuration.timeout = std::min(configuration.timeout, std::chrono::milliseconds(5000));
    beam::RoutePreference preference = beam::RoutePreference::automatic;
    if (options.contains("--route")) {
        const auto& route = options.at("--route");
        if (route == "direct")
            preference = beam::RoutePreference::direct;
        else if (route == "tailscale")
            preference = beam::RoutePreference::tailscale;
        else if (route != "auto")
            throw std::invalid_argument("route must be auto, direct or tailscale");
        if (options.contains("--endpoint"))
            throw std::invalid_argument("--route cannot be combined with --endpoint");
    }
    if (!manual) {
        beam::IdentityStore store(state_directory(options));
        if (receive) {
            configuration.credentials = store.credentials();
        } else {
            const auto peer = store.peer(positional[1]);
            const auto candidates =
                options.contains("--endpoint")
                    ? std::vector{beam::numeric_endpoint(options.at("--endpoint"))}
                    : beam::peer_endpoints(peer, preference);
            if (candidates.empty())
                throw std::invalid_argument(
                    "peer has no endpoint for this route; use --endpoint IP:PORT or "
                    "beam peer NAME --endpoint IP:PORT");
            endpoint = candidates.front();
            configuration.fallback_endpoints.assign(candidates.begin() + 1, candidates.end());
            if (candidates.size() > 1)
                configuration.connect_timeout =
                    std::min(configuration.timeout, std::chrono::milliseconds(3000));
            server_name = peer.identity.tls_name;
            configuration.credentials = store.credentials(peer.name);
        }
    }
    InterruptGuard interrupts;
    configuration.should_cancel = [] { return interrupted != 0; };
    auto last_update = std::chrono::steady_clock::time_point{};
    auto last_stage = beam::TransferStage::complete;
    if (!options.contains("--no-progress")) {
        if (!receive)
            configuration.on_connecting = [](const beam::Endpoint& target) {
                std::cerr << "Trying " << beam::endpoint_string(target) << '\n';
            };
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
            if (update.stage == beam::TransferStage::transferring)
                std::cerr << " ("
                          << (update.total ? static_cast<int>(
                                                 100.0L * static_cast<long double>(update.bytes) /
                                                 static_cast<long double>(update.total))
                                           : 0)
                          << "%, " << static_cast<std::uint64_t>(update.bytes_per_second)
                          << " B/s)";
            std::cerr << '\n';
        };
    }
    beam::TransferResult result;
    if (receive) {
        if (!positional.empty())
            throw std::invalid_argument("receive takes no positional arguments");
        const auto port = static_cast<std::uint16_t>(
            options.contains("--listen") ? number(options.at("--listen"), 65535, "listen port")
                                         : defaults.listen_port);
        const std::filesystem::path output = options.contains("--output")
                                                 ? std::filesystem::path(options.at("--output"))
                                                 : defaults.receive_folder;
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
            if (!options.contains("--no-discovery") && defaults.discovery) {
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
            std::cerr << "Listening for one authenticated peer on UDP port " << port << "..."
                      << std::endl;
        };
        do {
#ifdef BEAM_HAS_DISCOVERY
            advertisement.reset();
#endif
            result = beam::receive_content(port, output, configuration);
        } while (result.connectivity_check);
        if (result.kind == beam::ContentKind::text) {
            std::cout.write(result.text.data(), static_cast<std::streamsize>(result.text.size()));
            return 0;
        }
        std::cout << "Received and verified ";
    } else {
        if (checking) {
            beam::check_connectivity(endpoint, server_name, configuration);
            std::cout << positional[1] << " is reachable (authenticated)\n";
            return 0;
        }
        if (typed)
            result = beam::send_text(endpoint, filename, server_name, configuration);
        else
            result = beam::send_file(endpoint, filename, server_name, configuration);
        std::cout << "Sent and verified by peer ";
    }
    if (result.kind != beam::ContentKind::file)
        std::cout << beam::content_type(result.kind) << " ";
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
        if (command == "url")
            throw std::invalid_argument("URL mode was removed; use beam text URL PEER");
        if (command == "identity" || command == "pair" || command == "peers" ||
            command == "unpair" || command == "peer") {
#ifdef BEAM_HAS_QUIC
            return identity_command(argc, argv);
#else
            throw std::runtime_error("persistent identity is available in macOS/Linux QUIC builds");
#endif
        }
        if (command == "devices") {
            for (int index = 2; index < argc; ++index) {
                if (std::string_view(argv[index]) == "--tailscale") {
#ifdef BEAM_HAS_TAILSCALE
                    return tailscale_devices_command(argc, argv);
#else
                    throw std::runtime_error(
                        "Tailscale discovery is disabled in this build; see README.md");
#endif
                }
            }
#ifdef BEAM_HAS_DISCOVERY
            return devices_command(argc, argv);
#else
            throw std::runtime_error("LAN discovery is disabled in this build; see README.md");
#endif
        }
        if (command == "send" || command == "receive" || command == "text" || command == "check") {
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
#if defined(BEAM_HAS_DISCOVERY) || defined(BEAM_HAS_TAILSCALE)
    } catch (const beam::DiscoveryError& error) {
        std::cerr << "beam: " << error.what() << '\n';
        return error.cancelled ? 130 : 1;
#endif
    } catch (const std::exception& error) {
        std::cerr << "beam: " << error.what() << '\n';
        return 1;
    }
}
