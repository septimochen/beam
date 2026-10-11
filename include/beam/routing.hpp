#pragma once
#include "beam/identity.hpp"

namespace beam {
enum class RoutePreference { automatic, direct, tailscale };
// Strict numeric address parsing and canonicalization, including numeric IPv6 scopes.
[[nodiscard]] Endpoint numeric_endpoint(std::string_view text);
[[nodiscard]] std::string endpoint_string(const Endpoint& endpoint);
[[nodiscard]] bool is_tailscale_address(const std::string& host) noexcept;
// Direct address first, then configured Tailscale address; stable, deduplicated order.
// Discovery never changes trust or selects a peer by an unverified display name.
[[nodiscard]] std::vector<Endpoint>
peer_endpoints(const TrustedPeer& peer, RoutePreference preference = RoutePreference::automatic);
} // namespace beam
