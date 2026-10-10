#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace beam {
struct Credentials {
    std::filesystem::path certificate;
    std::filesystem::path private_key;
    std::filesystem::path ca_certificate;
    // Exact leaf certificates approved by explicit pairing. When nonempty, these
    // are the only trust anchors and accepted leaves; ca_certificate must be empty.
    std::vector<std::string> pinned_certificates{};
};
struct Endpoint {
    std::string host;
    std::uint16_t port;
};
[[nodiscard]] Endpoint parse_endpoint(const std::string& endpoint);
enum class TransferErrorCode : std::uint16_t {
    none = 0,
    cancelled = 1,
    rejected = 2,
    destination_exists = 3,
    io = 4,
    integrity = 5,
    size = 6,
    protocol = 7,
    timeout = 8,
    transport = 9,
    source_changed = 10
};
[[nodiscard]] const char* error_name(TransferErrorCode code) noexcept;
class TransferError : public std::runtime_error {
  public:
    TransferErrorCode code;
    bool remote;
    TransferError(TransferErrorCode category, const std::string& message, bool peer = false)
        : std::runtime_error(message), code(category), remote(peer) {}
};
enum class TransferStage { hashing, connecting, waiting, transferring, verifying, complete };
struct TransferProgress {
    TransferStage stage;
    std::string filename;
    std::uint64_t bytes;
    std::uint64_t total;
};
struct TransferOptions {
    Credentials credentials;
    std::chrono::milliseconds timeout{30000};
    // Called on the calling thread after the receive socket is bound.
    std::function<void()> on_listening{};
    std::stop_token stop_token{};
    // Callbacks run synchronously on the calling thread. Keep them short;
    // exceptions abort the transfer. Progress counts payload bytes, not wire bytes.
    std::function<void(const TransferProgress&)> on_progress{};
    // Optional calling-thread cancellation predicate (e.g. a signal flag).
    std::function<bool()> should_cancel{};
};
struct TransferResult {
    std::string filename;
    std::uint64_t bytes;
};
// One authenticated peer and one file per invocation. TransferError derives from
// std::runtime_error. Cancellation before publication removes the partial file;
// cancellation during the final exchange can leave a fully verified file.
TransferResult send_file(const Endpoint& endpoint, const std::filesystem::path& file,
                         const std::string& server_name, const TransferOptions& options);
TransferResult receive_file(std::uint16_t port, const std::filesystem::path& directory,
                            const TransferOptions& options);
} // namespace beam
