#pragma once
#include "beam/identity.hpp"

namespace beam {
// Strict numeric address parsing and canonicalization, including numeric IPv6 scopes.
[[nodiscard]] Endpoint numeric_endpoint(std::string_view text);
[[nodiscard]] std::string endpoint_string(const Endpoint& endpoint);
// Primary endpoint first, then optional fallback; stable, deduplicated order.
// Discovery never changes trust or selects a peer by an unverified display name.
[[nodiscard]] std::vector<Endpoint> peer_endpoints(const TrustedPeer& peer);
} // namespace beam
