#include "beam/beam.hpp"

#include <iostream>
#include <string_view>

namespace {
void print_help() {
    std::cout << "Beam — peer-to-peer sharing (bootstrap)\n\n"
                 "Usage:\n"
                 "  beam --help\n"
                 "  beam --version\n\n"
                 "Direct file transfer over QUIC is not implemented yet.\n";
}
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
    if (command == "send" || command == "receive") {
        std::cerr << "File transfer is not implemented yet; see docs/ROADMAP.md.\n";
        return 1;
    }
    std::cerr << "Unsupported command or arguments. Run beam --help.\n";
    return 2;
}
