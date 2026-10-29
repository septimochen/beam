#include "beam/beam.hpp"
#include "beam/config.hpp"
#ifdef BEAM_HAS_PAIRING
#include "beam/pairing.hpp"
#include <poll.h>
#include <thread>
#include <unistd.h>
#endif
#ifdef BEAM_HAS_DISCOVERY
#include "beam/discovery.hpp"
#endif
#ifdef BEAM_HAS_QUIC
#include "beam/identity.hpp"
#include "beam/routing.hpp"
#include "beam/transfer.hpp"
#include <unistd.h>
#endif
#include <array>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
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
           "  beam pairing host|join|forget [--state-dir DIR] [--name NAME]\n"
           "  beam pairing host --qr [--name NAME]\n"
           "  beam pair NAME --cert PEM --fingerprint SHA256 [--endpoint IP:PORT] [--state-dir "
           "DIR]\n"
           "  beam peers [--state-dir DIR]\n"
           "  beam peers export FILE [--state-dir DIR]\n"
           "  beam peers import FILE [--approve] [--state-dir DIR]\n"
           "  beam peer NAME [--rename NAME] [--endpoint IP:PORT | --clear-endpoint]\n"
           "  beam peer NAME --cert PEM --fingerprint VERIFIED_SHA256 [--state-dir DIR]\n"
           "  beam unpair NAME [--state-dir DIR]\n"
           "  beam peer NAME --endpoint IP:PORT [--state-dir DIR]\n"
           "  beam peer NAME --fallback-endpoint IP:PORT [--state-dir DIR]\n"
           "  beam peer NAME --clear-fallback [--state-dir DIR]\n"
           "  beam send FILE PEER [--endpoint IP:PORT] [--state-dir DIR] [--timeout SECONDS] "
           "[--no-progress]\n"
           "  beam text TEXT PEER [--endpoint IP:PORT] [--state-dir DIR] [--timeout SECONDS] "
           "[--no-progress]\n"
           "  beam check PEER [--endpoint IP:PORT] [--state-dir DIR] [--timeout SECONDS]\n"
           "  beam receive [--output DIR] [--listen PORT] [--state-dir DIR] [--name NAME] "
           "[--no-discovery] [--once]\n"
           "  beam devices [--timeout SECONDS]\n"
           "  beam receive --listen PORT [--output DIR] --cert PEM --key PEM --ca PEM "
           "[--timeout SECONDS] [--no-progress] [--name NAME] [--no-discovery] [--once]\n"
           "  beam send IP:PORT FILE --cert PEM --key PEM --ca PEM [--server-name NAME] "
           "[--timeout SECONDS] [--no-progress]\n\n"
           "Receive files and text until Ctrl-C; --once exits after one transfer.\n"
           "--endpoint overrides all saved routes. Fallback only precedes authentication.\n"
           "Files default to ~/Downloads; an explicit receive folder must exist.\n"
           "Received text is labeled and written to stdout.\n"
           "Text (including URLs): UTF-8, up to 64 KiB. Config: ~/.config/beam/config.\n"
           "Use -- before positional arguments that start with --.\n"
           "Use beam pairing, or approve public certificates with independently verified "
           "fingerprints.\n"
           "Legacy --cert/--key/--ca transfers still require a dedicated private CA.\n"
           "The server certificate must match IP or --server-name. IPv6: [ADDRESS]:PORT.\n"
           "Progress goes to stderr; --no-progress disables it. Ctrl-C cancels cleanly.\n"
           "Receivers advertise on LAN; --no-discovery disables this. Discovery is not trust.\n"
           "Transfer waits default to 30 seconds; --once also bounds the peer wait.\n"
           "Existing files are never overwritten.\n";
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
std::string human_size(long double bytes) {
    constexpr std::array units{"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    std::size_t unit = 0;
    while (bytes >= 1000 && unit + 1 < units.size()) {
        bytes /= 1000;
        ++unit;
    }
    std::ostringstream output;
    if (unit == 0)
        output << std::fixed << std::setprecision(0);
    else
        output << std::fixed << std::setprecision(2);
    output << bytes << ' ' << units[unit];
    return output.str();
}
class ProgressLine {
    bool active_ = false;

  public:
    const bool terminal = isatty(STDERR_FILENO) != 0;
    ~ProgressLine() { finish(); }
    void finish() {
        if (active_) {
            std::cerr << '\n' << std::flush;
            active_ = false;
        }
    }
    void update(const beam::TransferProgress& progress) {
        constexpr int width = 20;
        const auto fraction = progress.total
                                  ? std::min(1.0L, static_cast<long double>(progress.bytes) /
                                                       static_cast<long double>(progress.total))
                                  : 1.0L;
        const auto filled = static_cast<int>(fraction * width);
        const auto percent = static_cast<int>(fraction * 100);
        std::cerr << "\r\033[2K[" << std::string(static_cast<std::size_t>(filled), '=')
                  << std::string(static_cast<std::size_t>(width - filled), ' ') << "] " << percent
                  << "% " << human_size(progress.bytes) << " / " << human_size(progress.total)
                  << " (" << human_size(progress.bytes_per_second) << "/s)" << std::flush;
        active_ = true;
    }
};
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
#ifdef BEAM_HAS_PAIRING
std::string pairing_input(const char* prompt, const std::function<bool()>& active = {}) {
    std::cerr << prompt << std::flush;
    std::string result;
    // Bound stdin and remain responsive to Ctrl-C while waiting for a line.
    for (;;) {
        if (interrupted)
            throw beam::TransferError(beam::TransferErrorCode::cancelled, "Pairing cancelled");
        if (active && !active())
            throw std::runtime_error("Pairing no longer awaits approval");
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input, 1, 50);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            throw std::runtime_error("Read pairing input failed");
        if (!ready)
            continue;
        char byte = 0;
        if (read(STDIN_FILENO, &byte, 1) != 1)
            throw std::runtime_error("Pairing input closed");
        if (byte == '\n')
            return result;
        if (byte == '\r')
            continue;
        if (result.size() == 2048)
            throw std::invalid_argument("Pairing input exceeds bounds");
        result += byte;
    }
}
void print_pairing_qr(const std::vector<std::string>& rows) {
    const int size = static_cast<int>(rows.size());
    const auto dark = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < size && y < size &&
               rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '1';
    };
    for (int y = -4; y < size + 4; y += 2) {
        std::cout << "\033[30;47m";
        for (int x = -4; x < size + 4; ++x) {
            const bool top = dark(x, y), bottom = dark(x, y + 1);
            std::cout << (top ? (bottom ? "█" : "▀") : (bottom ? "▄" : " "));
        }
        std::cout << "\033[0m\n";
    }
}
int pairing_command(int argc, char** argv) {
    if (argc < 3)
        throw std::invalid_argument("Use beam pairing host|join|forget");
    const std::string command = argv[2];
    if (command != "host" && command != "join" && command != "forget")
        throw std::invalid_argument("Unknown pairing command");
    beam::PairingOptions options{beam::default_state_directory()};
    if (command == "host" || command == "join")
        options.transfer_port = beam::load_config(beam::default_config_file()).listen_port;
    bool qr = false;
    std::map<std::string, bool> seen;
    for (int index = 3; index < argc; ++index) {
        const std::string key = argv[index];
        if (!seen.emplace(key, true).second)
            throw std::invalid_argument("Duplicate pairing option");
        if (key == "--qr" && command == "host") {
            qr = true;
            continue;
        }
        if (++index >= argc)
            throw std::invalid_argument("Missing pairing option value");
        const std::string value = argv[index];
        if (key == "--state-dir")
            options.state_directory = value;
        else if (key == "--name")
            options.device_name = value;
        else if (key == "--port")
            options.transfer_port =
                static_cast<std::uint16_t>(number(value, 65535, "transfer port"));
        else
            throw std::invalid_argument("Unknown pairing option: " + key);
    }
    InterruptGuard interrupts;
    beam::PairingSession session(options);
    if (command == "forget") {
        session.forget_incomplete();
        return 0;
    }
    if (command == "host")
        session.host();
    else if (command == "join")
        session.join(pairing_input("Paste invitation: "));
    else
        throw std::invalid_argument("Unknown pairing command");
    beam::PairingState previous = beam::PairingState::idle;
    for (;;) {
        if (interrupted) {
            session.cancel();
            return 130;
        }
        const auto snapshot = session.snapshot();
        if (snapshot.state != previous) {
            previous = snapshot.state;
            std::cerr << beam::pairing_state_name(snapshot.state) << ": " << snapshot.message
                      << '\n';
            if (snapshot.state == beam::PairingState::hosting) {
                // stdout is the explicit invitation output channel, never a log.
                std::cout << snapshot.invitation << std::endl;
                if (qr)
                    print_pairing_qr(snapshot.qr);
            }
            if (snapshot.state == beam::PairingState::approval) {
                const auto answer = pairing_input(
                    ("Pair with " + snapshot.remote_name + " as " + snapshot.alias +
                     "? Type yes, or a different alias; no rejects: ")
                        .c_str(),
                    [&] { return session.snapshot().state == beam::PairingState::approval; });
                if (answer == "no") {
                    session.reject();
                    continue;
                }
                session.approve(answer == "yes" ? snapshot.alias : answer);
            }
            if (snapshot.state == beam::PairingState::paired)
                return 0;
            if (snapshot.state == beam::PairingState::failed ||
                snapshot.state == beam::PairingState::expired ||
                snapshot.state == beam::PairingState::incomplete ||
                snapshot.state == beam::PairingState::cancelled ||
                snapshot.state == beam::PairingState::rejected)
                return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
#endif
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
        if ((command == "peer" &&
             (argument == "--clear-fallback" || argument == "--clear-endpoint")) ||
            (command == "peers" && argument == "--approve")) {
            if (!options.emplace(argument, "").second)
                throw std::invalid_argument("duplicate option: " + argument);
            continue;
        }
        if (argument != "--state-dir" &&
            !(command == "pair" &&
              (argument == "--cert" || argument == "--fingerprint" || argument == "--endpoint")) &&
            !(command == "peer" &&
              (argument == "--endpoint" || argument == "--fallback-endpoint" ||
               argument == "--rename" || argument == "--cert" || argument == "--fingerprint")))
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
        if ((!positional.empty() && (positional.size() != 2 ||
                                     (positional[0] != "export" && positional[0] != "import"))) ||
            (options.contains("--approve") && (positional.empty() || positional[0] != "import")))
            throw std::invalid_argument(
                "use peers, peers export FILE, or peers import FILE [--approve]");
        beam::IdentityStore store(state_directory(options));
        if (positional.size() == 2 && positional[0] == "export") {
            if (options.contains("--approve"))
                throw std::invalid_argument("--approve is for import only");
            beam::write_device_backup(positional[1], store.export_devices());
            std::cout << "Saved device backup to " << positional[1]
                      << " (public certificates only).\n";
            return 0;
        }
        if (positional.size() == 2 && positional[0] == "import") {
            const auto contents = beam::read_device_backup(positional[1]);
            const auto backup = store.preview_import(contents);
            std::cout << "Backup source: " << backup.source_fingerprint << "\n";
            for (const auto& peer : backup.peers)
                std::cout << peer.name << '\t' << peer.endpoint << '\t' << peer.fallback_endpoint
                          << '\t' << peer.identity.fingerprint << '\n';
            std::cout << backup.added << " to restore, " << backup.existing
                      << " already paired (current settings kept).\n";
            if (backup.source_fingerprint != store.identity().fingerprint)
                std::cout << "This backup came from another identity. Other devices must pair with "
                             "this device before transfers work.\n";
            if (!options.contains("--approve")) {
                std::cout << "Review these fingerprints and trust the backup source. Run again "
                             "with --approve to restore.\n";
                return 0;
            }
            const auto result = store.import_devices(contents);
            std::cout << "Restored " << result.added << " devices.\n";
            return 0;
        }
        if (!positional.empty() || options.contains("--approve"))
            throw std::invalid_argument(
                "use peers, peers export FILE, or peers import FILE [--approve]");
        std::cout << "NAME\tENDPOINT\tFALLBACK\tFINGERPRINT\n";
        for (const auto& peer : store.peers())
            std::cout << peer.name << '\t' << (peer.endpoint.empty() ? "(unset)" : peer.endpoint)
                      << '\t'
                      << (peer.fallback_endpoint.empty() ? "(unset)" : peer.fallback_endpoint)
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
        std::cout << "Unpaired " << positional[0] << " on this device. Pair again to reconnect.\n";
    } else {
        if ((options.contains("--endpoint") && options.contains("--clear-endpoint")) ||
            (options.contains("--fallback-endpoint") && options.contains("--clear-fallback")))
            throw std::invalid_argument("choose a route value or clear it");
        if (options.contains("--cert") != options.contains("--fingerprint"))
            throw std::invalid_argument("certificate replacement needs --cert and --fingerprint");
        beam::IdentityStore store(state_directory(options));
        beam::PeerEdit edit;
        if (options.contains("--rename"))
            edit.name = required("--rename");
        if (options.contains("--endpoint"))
            edit.endpoint = required("--endpoint");
        if (options.contains("--clear-endpoint"))
            edit.endpoint = "";
        if (options.contains("--fallback-endpoint"))
            edit.fallback_endpoint = required("--fallback-endpoint");
        if (options.contains("--clear-fallback"))
            edit.fallback_endpoint = "";
        if (options.contains("--cert")) {
            edit.certificate_pem = beam::read_device_backup(required("--cert"));
            edit.verified_fingerprint = required("--fingerprint");
        }
        const bool changed =
            edit.name || edit.endpoint || edit.fallback_endpoint || !edit.certificate_pem.empty();
        const auto peer =
            changed ? store.edit_peer(positional[0], edit) : store.peer(positional[0]);
        std::cout << (changed ? "Updated " : "Device ") << peer.name
                  << "\nEndpoint: " << peer.endpoint << "\nFallback: " << peer.fallback_endpoint
                  << "\nFingerprint: " << peer.identity.fingerprint << '\n';
    }
    return 0;
}
int transfer_command(int argc, char** argv, bool receive) {
#ifdef __APPLE__
    // MsQuic's Darwin UDP sends can raise SIGPIPE. Report the bounded transport
    // failure instead of terminating before the normal error/cancellation path.
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        throw std::runtime_error("configure network signal handling failed");
#endif
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
        if (argument == "--no-progress" ||
            (receive && (argument == "--no-discovery" || argument == "--once"))) {
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
            !(!receive && (argument == "--server-name" || argument == "--endpoint")))
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
        if (options.contains("--state-dir") || options.contains("--endpoint"))
            throw std::invalid_argument(
                "manual credentials cannot be combined with --state-dir or --endpoint");
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
    if (!manual) {
        beam::IdentityStore store(state_directory(options));
        if (receive) {
            configuration.credentials = store.credentials();
        } else {
            const auto peer = store.peer(positional[1]);
            const auto candidates =
                options.contains("--endpoint")
                    ? std::vector{beam::numeric_endpoint(options.at("--endpoint"))}
                    : beam::peer_endpoints(peer);
            if (candidates.empty())
                throw std::invalid_argument("peer has no endpoint; use --endpoint IP:PORT or "
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
    ProgressLine progress_line;
    auto last_update = std::chrono::steady_clock::time_point{};
    auto last_stage = beam::TransferStage::complete;
    if (!options.contains("--no-progress")) {
        if (!receive)
            configuration.on_connecting = [&](const beam::Endpoint& target) {
                progress_line.finish();
                std::cerr << "Trying " << beam::endpoint_string(target) << '\n';
            };
        configuration.on_progress = [&](const beam::TransferProgress& update) {
            const auto now = std::chrono::steady_clock::now();
            // The final chunk may precede verification; only Complete denotes success.
            if (update.stage == last_stage && now - last_update < std::chrono::milliseconds(250))
                return;
            const bool new_stage = update.stage != last_stage;
            last_stage = update.stage;
            last_update = now;
            if (progress_line.terminal && update.stage == beam::TransferStage::transferring) {
                if (new_stage) {
                    progress_line.finish();
                    std::cerr << "Transferring";
                    if (!update.filename.empty())
                        std::cerr << " " << update.filename;
                    std::cerr << '\n';
                }
                progress_line.update(update);
                return;
            }
            progress_line.finish();
            std::cerr << stage_name(update.stage);
            if (!update.filename.empty())
                std::cerr << " " << update.filename;
            if (update.stage == beam::TransferStage::hashing ||
                update.stage == beam::TransferStage::transferring ||
                update.stage == beam::TransferStage::complete) {
                if (progress_line.terminal)
                    std::cerr << ": " << human_size(update.bytes) << " / "
                              << human_size(update.total);
                else
                    std::cerr << ": " << update.bytes << "/" << update.total << " bytes";
            }
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
        std::filesystem::path output = options.contains("--output")
                                           ? std::filesystem::path(options.at("--output"))
                                           : defaults.receive_folder;
        if (output.empty() && !options.contains("--output")) {
            output = std::filesystem::path(std::getenv("HOME")) / "Downloads";
            std::filesystem::create_directory(output);
        }
        std::cerr << "Receive directory: " << output.string() << '\n';
#ifdef BEAM_HAS_DISCOVERY
        std::unique_ptr<beam::ReceiverAdvertisement> advertisement;
        configuration.should_cancel = [&] {
            if (advertisement) {
                try {
                    advertisement->check();
                } catch (const std::exception& error) {
                    progress_line.finish();
                    std::cerr << "beam: LAN advertisement stopped: " << error.what() << '\n';
                    advertisement.reset();
                }
            }
            return interrupted != 0;
        };
#endif
        configuration.wait_for_peer_indefinitely = !options.contains("--once");
        bool listening = false;
        configuration.on_listening = [&] {
            listening = true;
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
        for (;;) {
#ifdef BEAM_HAS_DISCOVERY
            advertisement.reset();
#endif
            listening = false;
            try {
                result = beam::receive_content(port, output, configuration);
            } catch (const beam::TransferError& error) {
                progress_line.finish();
                if (!listening || options.contains("--once") ||
                    error.code == beam::TransferErrorCode::cancelled)
                    throw;
                std::cerr << "beam [" << beam::error_name(error.code) << "]: " << error.what()
                          << '\n';
                continue;
            }
            if (result.connectivity_check)
                continue;
            if (result.kind == beam::ContentKind::text) {
                std::cout << "Received text: ";
                std::cout.write(result.text.data(),
                                static_cast<std::streamsize>(result.text.size()));
                std::cout << '\n' << std::flush;
            } else {
                std::cerr << "Received and verified " << result.filename << " (";
                if (progress_line.terminal)
                    std::cerr << human_size(result.bytes);
                else
                    std::cerr << result.bytes << " bytes";
                std::cerr << ", SHA-256)\n";
            }
            if (options.contains("--once"))
                return 0;
        }
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
    std::cout << result.filename << " (";
    if (isatty(STDOUT_FILENO))
        std::cout << human_size(result.bytes);
    else
        std::cout << result.bytes << " bytes";
    std::cout << ", SHA-256)\n";
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
#ifdef BEAM_HAS_DISCOVERY
            return devices_command(argc, argv);
#else
            throw std::runtime_error("LAN discovery is disabled in this build; see README.md");
#endif
        }
        if (command == "pairing") {
#ifdef BEAM_HAS_PAIRING
            return pairing_command(argc, argv);
#else
            throw std::runtime_error("Network pairing is disabled in this build");
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
