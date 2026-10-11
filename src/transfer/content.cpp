#include "beam/transfer.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <charconv>

namespace beam {
std::string_view content_type(ContentKind kind) noexcept {
    switch (kind) {
    case ContentKind::file:
        return "application/octet-stream";
    case ContentKind::text:
        return "text/plain";
    case ContentKind::url:
        return "text/uri-list";
    }
    return {};
}
namespace {
bool valid_text(std::string_view text) {
    // Decode Unicode scalar values strictly: reject overlong encodings,
    // surrogate code points, truncated sequences, and values above U+10FFFF.
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        std::uint32_t value = first;
        unsigned remaining = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            value = first & 0x1fU;
            remaining = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            value = first & 0x0fU;
            remaining = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value = first & 0x07U;
            remaining = 3;
            minimum = 0x10000;
        } else if (first >= 0x80) {
            return false;
        }
        if (remaining > text.size() - index)
            return false;
        for (unsigned next = 0; next < remaining; ++next) {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & 0xc0U) != 0x80U)
                return false;
            value = (value << 6) | (byte & 0x3fU);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            (value < 32 && value != 9 && value != 10 && value != 13) ||
            (value >= 0x7f && value <= 0x9f))
            return false;
    }
    return true;
}
bool alphanumeric(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
bool hexadecimal(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool valid_host(std::string_view host) {
    if (host.empty() || host.size() > 253)
        return false;
    if (host.back() == '.')
        host.remove_suffix(1);
    if (host.empty())
        return false;
    while (!host.empty()) {
        const auto dot = host.find('.');
        const auto label = host.substr(0, dot);
        if (label.empty() || label.size() > 63 || !alphanumeric(label.front()) ||
            !alphanumeric(label.back()) || !std::all_of(label.begin(), label.end(), [](char c) {
                return alphanumeric(c) || c == '-';
            }))
            return false;
        if (dot == std::string_view::npos)
            break;
        host.remove_prefix(dot + 1);
        if (host.empty())
            return false;
    }
    return true;
}
bool valid_url(std::string_view url) {
    // Validate the whole input before passing an IPv6 literal to a C-string API.
    // Otherwise a NUL inside its host could hide an invalid suffix from inet_pton.
    if (!std::all_of(url.begin(), url.end(), [](unsigned char c) { return c > 32 && c < 127; }))
        return false;
    const auto separator = url.find("://");
    if (separator != 4 && separator != 5)
        return false;
    std::string scheme(url.substr(0, separator));
    for (auto& c : scheme)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    if (scheme != "http" && scheme != "https")
        return false;
    url.remove_prefix(separator + 3);
    const auto end = url.find_first_of("/?#");
    auto authority = url.substr(0, end);
    std::string_view port;
    if (authority.empty() || authority.find('@') != std::string_view::npos)
        return false;
    if (authority.front() == '[') {
        const auto bracket = authority.find(']');
        if (bracket == std::string_view::npos)
            return false;
        in6_addr address{};
        const std::string host(authority.substr(1, bracket - 1));
        if (inet_pton(AF_INET6, host.c_str(), &address) != 1)
            return false;
        authority.remove_prefix(bracket + 1);
        if (!authority.empty()) {
            if (authority.front() != ':')
                return false;
            port = authority;
        }
    } else {
        const auto colon = authority.find(':');
        if (!valid_host(authority.substr(0, colon)))
            return false;
        if (colon != std::string_view::npos)
            port = authority.substr(colon);
    }
    if (!port.empty()) {
        port.remove_prefix(1);
        unsigned value = 0;
        const auto [last, error] = std::from_chars(port.data(), port.data() + port.size(), value);
        if (error != std::errc{} || last != port.data() + port.size() || value == 0 ||
            value > 65535)
            return false;
    }
    if (end == std::string_view::npos)
        return true;
    url.remove_prefix(end);
    bool fragment = false;
    for (std::size_t index = 0; index < url.size(); ++index) {
        const char c = url[index];
        if (c == '%') {
            if (url.size() - index < 3 || !hexadecimal(url[index + 1]) ||
                !hexadecimal(url[index + 2]))
                return false;
            index += 2;
        } else if (c == '#') {
            if (fragment)
                return false;
            fragment = true;
        } else if (!alphanumeric(c) &&
                   std::string_view("-._~!$&'()*+,;=:@/?").find(c) == std::string_view::npos) {
            return false;
        }
    }
    return true;
}
} // namespace
void validate_content(ContentKind kind, std::string_view content) {
    if (kind == ContentKind::text) {
        if (content.size() > max_text_size || !valid_text(content))
            throw std::invalid_argument("text must be valid UTF-8 without NUL/control characters "
                                        "(tabs/newlines allowed), at most 65536 bytes");
    } else if (kind == ContentKind::url) {
        if (content.empty() || content.size() > max_url_size || !valid_url(content))
            throw std::invalid_argument("URL must be one absolute HTTP(S) URL without credentials, "
                                        "using ASCII/percent encoding, at most 8192 bytes");
    } else {
        throw std::invalid_argument("expected text or URL content kind");
    }
}
} // namespace beam
