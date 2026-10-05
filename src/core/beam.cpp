#include "beam/beam.hpp"

#include <array>

namespace beam {

std::string_view version() noexcept { return BEAM_VERSION; }

bool is_safe_filename(std::string_view filename) noexcept {
    // Conservative policy until the protocol defines Unicode normalization.
    if (filename.empty() || filename.size() > 255 || filename.front() == '.' ||
        filename.front() == ' ' || filename.back() == '.' || filename.back() == ' ') {
        return false;
    }
    for (const char character : filename) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 32 || byte > 126 ||
            std::string_view{"/\\:<>\"|?*"}.find(character) != std::string_view::npos) {
            return false;
        }
    }

    // Windows reserves device basenames even when followed by an extension.
    const auto basename = filename.substr(0, filename.find('.'));
    if (basename.back() == ' ') {
        return false;
    }
    std::array<char, 255> uppercase{};
    for (std::size_t index = 0; index < basename.size(); ++index) {
        const char character = basename[index];
        uppercase[index] = character >= 'a' && character <= 'z'
                               ? static_cast<char>(character - 'a' + 'A')
                               : character;
    }
    const std::string_view name{uppercase.data(), basename.size()};
    if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL" || name == "CONIN$" ||
        name == "CONOUT$") {
        return false;
    }
    if (name.size() == 4 && (name.starts_with("COM") || name.starts_with("LPT")) &&
        name[3] >= '1' && name[3] <= '9') {
        return false;
    }
    return true;
}

} // namespace beam
