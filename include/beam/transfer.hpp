#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace beam {
struct Credentials {
    std::filesystem::path certificate;
    std::filesystem::path private_key;
    std::filesystem::path ca_certificate;
};
struct Endpoint {
    std::string host;
    std::uint16_t port;
};
[[nodiscard]] Endpoint parse_endpoint(const std::string& endpoint);
struct TransferOptions {
    Credentials credentials;
    std::chrono::milliseconds timeout{30000};
    // Called on the calling thread after the receive socket is bound.
    std::function<void()> on_listening{};
};
struct TransferResult {
    std::string filename;
    std::uint64_t bytes;
};
// One authenticated peer and one file per invocation. Errors throw std::runtime_error.
TransferResult send_file(const Endpoint& endpoint, const std::filesystem::path& file,
                         const std::string& server_name, const TransferOptions& options);
TransferResult receive_file(std::uint16_t port, const std::filesystem::path& directory,
                            const TransferOptions& options);
} // namespace beam
