#pragma once
#include "beam/discovery.hpp"

namespace beam {
struct TailscaleDevice {
    std::string name;
    bool online;
    std::vector<Endpoint> endpoints;
};
// Only TailscaleIPs are used, never WireGuard transport addresses or subnet routes.
// Results are address hints: they do not prove a Beam receiver is running or paired.
[[nodiscard]] std::vector<TailscaleDevice> parse_tailscale_status(std::string_view json,
                                                                  std::uint16_t port = 4269);
// Executes exactly `tailscale status --json`, without a shell, with bounded output,
// cancellation, a total deadline, and child termination/reaping on all failure paths.
[[nodiscard]] std::vector<TailscaleDevice>
discover_tailscale(const DiscoveryOptions& options = {}, std::uint16_t port = 4269,
                   const std::string& executable = "tailscale");
} // namespace beam
