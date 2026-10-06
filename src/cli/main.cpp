#include "beam/beam.hpp"
#ifdef BEAM_HAS_QUIC
#include "beam/transfer.hpp"
#endif
#include <charconv>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void print_help() {
    std::cout << "Beam — direct file transfer over QUIC\n\n"
                 "Usage:\n"
                 "  beam --help\n"
                 "  beam --version\n"
                 "  beam receive --listen PORT --output DIR --cert PEM --key PEM --ca PEM "
                 "[--timeout SECONDS]\n"
                 "  beam send IP:PORT FILE --cert PEM --key PEM --ca PEM [--server-name NAME] "
                 "[--timeout SECONDS]\n\n"
                 "Receive one file from one authenticated peer, then exit. DIR must exist.\n"
                 "Both peers need certificates issued by the private CA supplied in --ca.\n"
                 "The server certificate must match IP or --server-name. IPv6: [ADDRESS]:PORT.\n"
                 "Timeout defaults to 30 seconds per wait. Existing files are never overwritten.\n";
}
#ifdef BEAM_HAS_QUIC
unsigned number(const std::string& text, unsigned maximum, const char* label) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > maximum)
        throw std::invalid_argument(std::string("invalid ") + label);
    return value;
}
int transfer_command(int argc, char** argv, bool receive) {
    std::map<std::string, std::string> options;
    std::vector<std::string> positional;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (!argument.starts_with("--")) {
            positional.push_back(argument);
            continue;
        }
        if (argument != "--cert" && argument != "--key" && argument != "--ca" &&
            argument != "--timeout" &&
            !(receive && (argument == "--listen" || argument == "--output")) &&
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
    if (options.contains("--timeout"))
        configuration.timeout =
            std::chrono::seconds(number(options.at("--timeout"), 86400, "timeout"));
    beam::TransferResult result;
    if (receive) {
        if (!positional.empty())
            throw std::invalid_argument("receive takes no positional arguments");
        const auto port =
            static_cast<std::uint16_t>(number(required("--listen"), 65535, "listen port"));
        const auto output = required("--output");
        configuration.on_listening = [port] {
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
    } catch (const std::exception& error) {
        std::cerr << "beam: " << error.what() << '\n';
        return 1;
    }
}
