#include "protocol/pairing.hpp"
#include "beam/routing.hpp"
#include "qrcodegen.hpp"
#include <algorithm>
#include <array>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace beam::pairing_protocol {
bool hex_string(std::string_view text, std::size_t size) noexcept {
    return text.size() == size && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string random_hex(std::size_t bytes) {
    if (bytes > 32)
        throw std::invalid_argument("pairing random size exceeds bounds");
    std::array<unsigned char, 32> data{};
    if (RAND_bytes(data.data(), static_cast<int>(bytes)) != 1)
        throw std::runtime_error("pairing random generation failed");
    std::string out;
    for (std::size_t i = 0; i < bytes; ++i) {
        out += "0123456789abcdef"[data[i] >> 4];
        out += "0123456789abcdef"[data[i] & 15];
    }
    return out;
}
std::string pack(const std::vector<std::string>& fields) {
    if (fields.size() > 16)
        throw std::invalid_argument("too many pairing fields");
    std::string out;
    for (const auto& field : fields) {
        if (field.size() > max_frame || out.size() + field.size() + 4 > max_frame)
            throw std::invalid_argument("pairing frame exceeds bounds");
        const auto size = static_cast<std::uint32_t>(field.size());
        for (int shift = 24; shift >= 0; shift -= 8)
            out += static_cast<char>((size >> shift) & 255);
        out += field;
    }
    return out;
}
std::vector<std::string> unpack(std::string_view bytes) {
    if (bytes.size() > max_frame)
        throw std::invalid_argument("pairing frame exceeds bounds");
    std::vector<std::string> out;
    while (!bytes.empty()) {
        if (bytes.size() < 4 || out.size() == 16)
            throw std::invalid_argument("invalid pairing frame");
        std::uint32_t count = 0;
        for (const auto c : bytes.substr(0, 4))
            count = (count << 8) | static_cast<unsigned char>(c);
        bytes.remove_prefix(4);
        if (count > bytes.size())
            throw std::invalid_argument("truncated pairing field");
        auto field = bytes.substr(0, count);
        if (field.find('\0') != std::string_view::npos)
            throw std::invalid_argument("NUL in pairing field");
        out.emplace_back(field);
        bytes.remove_prefix(count);
    }
    return out;
}
std::string encode(const Invitation& invitation) {
    std::vector<std::string> fields{"1", invitation.id, invitation.fingerprint, invitation.secret};
    for (const auto& endpoint : invitation.endpoints)
        fields.push_back(endpoint_string(endpoint));
    const auto bytes = pack(fields);
    std::string out(4 * ((bytes.size() + 2) / 3), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                    reinterpret_cast<const unsigned char*>(bytes.data()),
                    static_cast<int>(bytes.size()));
    std::replace(out.begin(), out.end(), '+', '-');
    std::replace(out.begin(), out.end(), '/', '_');
    while (out.ends_with('='))
        out.pop_back();
    return "beam-pair:1:" + out;
}
Invitation decode(std::string_view text) {
    constexpr std::string_view prefix = "beam-pair:1:";
    if (text.size() > 2048 || !text.starts_with(prefix))
        throw std::invalid_argument("invalid or unsupported Beam pairing invitation");
    std::string encoded(text.substr(prefix.size()));
    if (encoded.empty() || encoded.size() % 4 == 1 ||
        !std::all_of(encoded.begin(), encoded.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_';
        }))
        throw std::invalid_argument("invalid invitation encoding");
    std::replace(encoded.begin(), encoded.end(), '-', '+');
    std::replace(encoded.begin(), encoded.end(), '_', '/');
    const auto padding = (4 - encoded.size() % 4) % 4;
    encoded.append(padding, '=');
    std::string bytes(3 * (encoded.size() / 4), '\0');
    const int size = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(bytes.data()),
                                     reinterpret_cast<const unsigned char*>(encoded.data()),
                                     static_cast<int>(encoded.size()));
    if (size < 0)
        throw std::invalid_argument("invalid invitation encoding");
    bytes.resize(static_cast<std::size_t>(size) - padding);
    const auto fields = unpack(bytes);
    if (fields.size() < 5 || fields.size() > 12 || fields[0] != "1" || !hex_string(fields[1], 32) ||
        !hex_string(fields[2], 64) || !hex_string(fields[3], 64))
        throw std::invalid_argument("invalid invitation fields");
    Invitation result{fields[1], fields[2], fields[3], {}};
    for (std::size_t i = 4; i < fields.size(); ++i)
        result.endpoints.push_back(numeric_endpoint(fields[i]));
    if (encode(result) != text)
        throw std::invalid_argument("noncanonical invitation");
    return result;
}
std::string suggested_alias(std::string_view name, const std::vector<TrustedPeer>& peers,
                            std::string_view fingerprint) {
    for (const auto& peer : peers)
        if (peer.identity.fingerprint == fingerprint)
            return peer.name;
    std::string base;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            base += c;
        else if (c == ' ')
            base += '-';
        if (base.size() == 48)
            break;
    }
    if (base.empty() || base.front() < 'a' || base.front() > 'z')
        base = "device-" + base;
    auto alias = base;
    for (unsigned index = 2; std::any_of(peers.begin(), peers.end(),
                                         [&](const auto& peer) { return peer.name == alias; });
         ++index)
        alias = base + "-" + std::to_string(index);
    return alias;
}
} // namespace beam::pairing_protocol
namespace beam {
std::vector<std::string> pairing_qr(const std::string& invitation) {
    static_cast<void>(pairing_protocol::decode(invitation));
    const auto qr =
        qrcodegen::QrCode::encodeText(invitation.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    std::vector<std::string> rows;
    for (int y = 0; y < qr.getSize(); ++y) {
        std::string row;
        for (int x = 0; x < qr.getSize(); ++x)
            row += qr.getModule(x, y) ? '1' : '0';
        rows.push_back(std::move(row));
    }
    return rows;
}
} // namespace beam
