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
    const bool ipv6 = inet_pton(AF_INET6, address.c_str(), bytes.data()) == 1;
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
bool is_tailscale_address(const std::string& host) noexcept {
    if (host.find('\0') != std::string::npos || host.find('%') != std::string::npos)
        return false;
    std::array<unsigned char, 16> bytes{};
    if (inet_pton(AF_INET, host.c_str(), bytes.data()) == 1)
        return bytes[0] == 100 && (bytes[1] & 0xc0U) == 64;
    if (inet_pton(AF_INET6, host.c_str(), bytes.data()) != 1)
        return false;
    const std::array<unsigned char, 6> prefix{0xfd, 0x7a, 0x11, 0x5c, 0xa1, 0xe0};
    if (std::equal(prefix.begin(), prefix.end(), bytes.begin()))
        return true;
    // IPv4-mapped IPv6 must use the same classification as its IPv4 address.
    return std::all_of(bytes.begin(), bytes.begin() + 10, [](auto byte) { return byte == 0; }) &&
           bytes[10] == 0xff && bytes[11] == 0xff && bytes[12] == 100 && (bytes[13] & 0xc0U) == 64;
}
std::vector<Endpoint> peer_endpoints(const TrustedPeer& peer, RoutePreference preference) {
    std::vector<Endpoint> direct, tailscale;
    if (!peer.endpoint.empty()) {
        const auto endpoint = numeric_endpoint(peer.endpoint);
        (is_tailscale_address(endpoint.host) ? tailscale : direct).push_back(endpoint);
    }
    if (!peer.tailscale_endpoint.empty()) {
        const auto endpoint = numeric_endpoint(peer.tailscale_endpoint);
        if (!is_tailscale_address(endpoint.host))
            throw std::invalid_argument(
                "Tailscale endpoint must use 100.64.0.0/10 or fd7a:115c:a1e0::/48");
        if (tailscale.empty() || endpoint_string(tailscale.front()) != endpoint_string(endpoint))
            tailscale.push_back(endpoint);
    }
    if (preference == RoutePreference::direct)
        return direct;
    if (preference == RoutePreference::tailscale)
        return tailscale;
    direct.insert(direct.end(), tailscale.begin(), tailscale.end());
    return direct;
}
} // namespace beam
