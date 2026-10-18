#pragma once
#include "beam/pairing.hpp"
#include <span>

namespace beam::pairing_protocol {
constexpr std::size_t max_frame = 24576;
struct Invitation {
    std::string id, fingerprint, secret;
    std::vector<Endpoint> endpoints;
};
std::string pack(const std::vector<std::string>& fields);
std::vector<std::string> unpack(std::string_view bytes);
std::string encode(const Invitation& invitation);
Invitation decode(std::string_view invitation);
bool hex_string(std::string_view text, std::size_t size) noexcept;
std::string random_hex(std::size_t bytes);
std::string suggested_alias(std::string_view name, const std::vector<TrustedPeer>& peers,
                            std::string_view fingerprint);
} // namespace beam::pairing_protocol
