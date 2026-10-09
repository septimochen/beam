#pragma once

#include "beam/transfer.hpp"
#include <memory>
#include <string_view>
#include <vector>

namespace beam {
// Discovery records are untrusted hints, never device identities or TLS names.
struct DiscoveredDevice {
    std::string name;
    std::string hostname;
    std::uint32_t interface_index;
    std::vector<Endpoint> endpoints;
};
struct DiscoveryOptions {
    std::chrono::milliseconds timeout{3000};
    std::stop_token stop_token{};
    std::function<bool()> should_cancel{};
};
class DiscoveryError : public std::runtime_error {
  public:
    bool cancelled;
    explicit DiscoveryError(const std::string& message, bool was_cancelled = false)
        : std::runtime_error(message), cancelled(was_cancelled) {}
};
[[nodiscard]] bool is_discovery_name(std::string_view name) noexcept;
[[nodiscard]] std::vector<DiscoveredDevice> discover_devices(const DiscoveryOptions& options = {});

// Register only after the QUIC listener is bound. Destruction withdraws the
// service and joins its worker before releasing callback contexts.
class ReceiverAdvertisement {
    struct Impl;
    std::unique_ptr<Impl> implementation;

  public:
    ReceiverAdvertisement(std::uint16_t port, const std::string& name = "Beam",
                          const DiscoveryOptions& options = {});
    ~ReceiverAdvertisement();
    ReceiverAdvertisement(const ReceiverAdvertisement&) = delete;
    ReceiverAdvertisement& operator=(const ReceiverAdvertisement&) = delete;
    [[nodiscard]] std::string name() const;
    // Surface asynchronous daemon failures on the application thread.
    void check() const;
};
} // namespace beam
