#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace beam {
struct AppConfig {
    std::filesystem::path receive_folder;
    std::uint16_t listen_port{4269};
    bool discovery{true};
    std::chrono::milliseconds timeout{30000};
};
[[nodiscard]] std::filesystem::path default_config_file();
// Missing files use defaults. Invalid/oversized configuration fails with a
// line-specific error; configuration never creates a receive directory.
[[nodiscard]] AppConfig load_config(const std::filesystem::path& file);
} // namespace beam
