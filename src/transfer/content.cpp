#include "beam/transfer.hpp"

namespace beam {
std::string_view content_type(ContentKind kind) noexcept {
    switch (kind) {
    case ContentKind::file:
        return "application/octet-stream";
    case ContentKind::text:
        return "text/plain";
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
} // namespace
void validate_content(ContentKind kind, std::string_view content) {
    if (kind == ContentKind::text) {
        if (content.size() > max_text_size || !valid_text(content))
            throw std::invalid_argument("text must be valid UTF-8 without NUL/control characters "
                                        "(tabs/newlines allowed), at most 65536 bytes");
    } else {
        throw std::invalid_argument("expected text content kind");
    }
}
} // namespace beam
