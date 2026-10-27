#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
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
enum class ContentKind : std::uint8_t { file = 1, text = 2 };
inline constexpr std::size_t max_text_size = 65536;
[[nodiscard]] std::string_view content_type(ContentKind kind) noexcept;
// Throws invalid_argument for invalid UTF-8 text. URLs are ordinary text.
// Text allows tabs/newlines, but no NUL or other C0/C1 controls.
void validate_content(ContentKind kind, std::string_view content);
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
    double bytes_per_second{};
};
struct TransferOptions {
    Credentials credentials;
    std::chrono::milliseconds timeout{30000};
    // Wait without a deadline for an incoming connection; handshake and transfer waits remain
    // bounded.
    bool wait_for_peer_indefinitely{false};
    // Called on the calling thread after the receive socket is bound.
    std::function<void()> on_listening{};
    std::stop_token stop_token{};
    // Callbacks run synchronously on the calling thread. Keep them short;
    // exceptions abort the transfer. Progress counts payload bytes, not wire bytes.
    std::function<void(const TransferProgress&)> on_progress{};
    // Optional calling-thread cancellation predicate (e.g. a signal flag).
    std::function<bool()> should_cancel{};
    // Sender-only. Retry these only for pre-authentication connectivity failures.
    // No fallback after authentication, an offer, a rejection, or payload bytes.
    std::vector<Endpoint> fallback_endpoints{};
    // Zero retains timeout for connection setup. CLI caps attempts at 3 seconds
    // when multiple routes are available; this never changes payload wait limits.
    std::chrono::milliseconds connect_timeout{};
    std::function<void(const Endpoint&)> on_connecting{};
};
struct TransferResult {
    std::string filename;
    std::uint64_t bytes{};
    ContentKind kind{ContentKind::file};
    std::string text{};
    // An authenticated probe has no payload and creates no received content.
    bool connectivity_check{false};
};
// One authenticated peer and one typed payload per invocation. TransferError derives from
// std::runtime_error. Cancellation before publication removes the partial file;
// cancellation during the final exchange can leave a fully verified file.
TransferResult send_file(const Endpoint& endpoint, const std::filesystem::path& file,
                         const std::string& server_name, const TransferOptions& options);
TransferResult send_text(const Endpoint& endpoint, std::string_view text,
                         const std::string& server_name, const TransferOptions& options);
void check_connectivity(const Endpoint& endpoint, const std::string& server_name,
                        const TransferOptions& options);
// Text is validated in bounded memory and returned in result.text; only files
// touch the receive directory. A connectivity check returns no content.
TransferResult receive_content(std::uint16_t port, const std::filesystem::path& directory,
                               const TransferOptions& options);
// File-only API: rejects text offers before accepting payload bytes.
TransferResult receive_file(std::uint16_t port, const std::filesystem::path& directory,
                            const TransferOptions& options);
} // namespace beam
