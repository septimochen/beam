#include "beam/config.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("beam-config-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directory(root);
        const auto file = root / "config";
        const auto defaults = beam::load_config(file);
        if (defaults.listen_port != 4269 || !defaults.discovery || !defaults.receive_folder.empty())
            throw std::logic_error("invalid config defaults");
        std::ofstream(file) << "# Settings\nreceive_folder = "
                            << std::quoted((root / "receive folder").string())
                            << "\nlisten_port=5678\ndiscovery=false\ntimeout_seconds=12\n";
        const auto config = beam::load_config(file);
        if (config.receive_folder != root / "receive folder" || config.listen_port != 5678 ||
            config.discovery || config.timeout != std::chrono::seconds(12))
            throw std::logic_error("config values lost");
        for (const auto& invalid :
             {std::string("unknown = true"), std::string("listen_port=0"),
              std::string("listen_port=65536"), std::string("listen_port=2\nlisten_port=3"),
              std::string("timeout_seconds=86401"), std::string("discovery=yes"),
              std::string("receive_folder=\"../relative\""),
              std::string("receive_folder=\"/tmp\" extra"),
              std::string("receive_folder=\"/tmp\npath\""), std::string(16385, 'x'),
              std::string("a\0b", 3)}) {
            std::ofstream(file, std::ios::binary | std::ios::trunc) << invalid;
            bool rejected = false;
            try {
                static_cast<void>(beam::load_config(file));
            } catch (const std::exception&) {
                rejected = true;
            }
            if (!rejected)
                throw std::logic_error("invalid config accepted");
        }
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
