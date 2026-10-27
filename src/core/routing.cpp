#include "beam/routing.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <charconv>

namespace beam {
Endpoint numeric_endpoint(std::string_view text) {
    if (text.empty() || text.size() > 128 ||
        text.find_first_of("\r\n\t ") != std::string_view::npos ||
        text.find('\0') != std::string_view::npos)
        throw std::invalid_argument("invalid numeric endpoint");
    Endpoint endpoint;
    try {
        endpoint = parse_endpoint(std::string(text));
    } catch (const std::runtime_error& error) {
        throw std::invalid_argument(error.what());
    }
    const auto scope = endpoint.host.find('%');
    const auto address = endpoint.host.substr(0, scope);
    std::array<unsigned char, 16> bytes{};
    bool ipv6 = inet_pton(AF_INET6, address.c_str(), bytes.data()) == 1;
    if (!ipv6 &&
        (scope != std::string::npos || inet_pton(AF_INET, address.c_str(), bytes.data()) != 1))
        throw std::invalid_argument("endpoint must contain a numeric IPv4 or IPv6 address");
    std::string suffix;
    if (scope != std::string::npos) {
        const auto index_text = std::string_view(endpoint.host).substr(scope + 1);
        std::uint32_t index = 0;
        const auto [end, error] =
            std::from_chars(index_text.data(), index_text.data() + index_text.size(), index);
        if (!ipv6 || index == 0 || error != std::errc{} ||
            end != index_text.data() + index_text.size())
            throw std::invalid_argument(
                "IPv6 scope must be a nonzero 32-bit numeric interface index");
        suffix = "%" + std::to_string(index);
    }
    // MsQuic must connect to mapped addresses as IPv4. Canonicalize here so
    // equivalent saved routes also collapse before a fallback is attempted.
    if (ipv6 && suffix.empty() &&
        std::all_of(bytes.begin(), bytes.begin() + 10, [](auto byte) { return byte == 0; }) &&
        bytes[10] == 0xff && bytes[11] == 0xff) {
        std::copy_n(bytes.begin() + 12, 4, bytes.begin());
        ipv6 = false;
    }
    std::array<char, INET6_ADDRSTRLEN> formatted{};
    if (!inet_ntop(ipv6 ? AF_INET6 : AF_INET, bytes.data(), formatted.data(), formatted.size()))
        throw std::runtime_error("format numeric endpoint failed");
    endpoint.host = std::string(formatted.data()) + suffix;
    return endpoint;
}
std::string endpoint_string(const Endpoint& endpoint) {
    return (endpoint.host.find(':') == std::string::npos ? endpoint.host
                                                         : "[" + endpoint.host + "]") +
           ":" + std::to_string(endpoint.port);
}
std::vector<Endpoint> peer_endpoints(const TrustedPeer& peer) {
    std::vector<Endpoint> endpoints;
    if (!peer.endpoint.empty())
        endpoints.push_back(numeric_endpoint(peer.endpoint));
    if (!peer.fallback_endpoint.empty()) {
        const auto fallback = numeric_endpoint(peer.fallback_endpoint);
        if (endpoints.empty() || endpoint_string(endpoints.front()) != endpoint_string(fallback))
            endpoints.push_back(fallback);
    }
    return endpoints;
}
} // namespace beam
