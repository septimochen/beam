#include "beam/config.hpp"
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace beam {
std::filesystem::path default_config_file() {
    const auto* home = std::getenv("HOME");
    if (!home || !*home)
        throw std::invalid_argument("HOME is required to locate ~/.config/beam/config");
    return std::filesystem::path(home) / ".config/beam/config";
}
AppConfig load_config(const std::filesystem::path& file) {
    AppConfig config;
    if (!std::filesystem::exists(file))
        return config;
    if (!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file) > 16384)
        throw std::invalid_argument("Beam config must be a regular file of at most 16 KiB");
    std::ifstream input(file);
    if (!input)
        throw std::invalid_argument("Cannot read Beam config: " + file.string());
    std::string source(16385, '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    source.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad() || source.size() > 16384 || source.find('\0') != std::string::npos)
        throw std::invalid_argument("Cannot read bounded Beam config");
    const auto trim = [](std::string text) {
        const auto first = text.find_first_not_of(" \t\r");
        return first == std::string::npos
                   ? std::string{}
                   : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    };
    std::istringstream lines(source);
    std::set<std::string> keys;
    std::string line;
    unsigned number = 0;
    while (std::getline(lines, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        const auto fail = [&] {
            throw std::invalid_argument("Invalid Beam config at line " + std::to_string(number));
        };
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            fail();
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (!keys.insert(key).second || value.empty())
            fail();
        if (key == "receive_folder") {
            std::istringstream quoted(value);
            std::string path;
            if (value.front() != '"' || !(quoted >> std::quoted(path)))
                fail();
            quoted >> std::ws;
            if (!quoted.eof() || path.empty())
                fail();
            for (const auto character : path)
                if (static_cast<unsigned char>(character) < 32 || character == 127)
                    fail();
            if (path.starts_with("~/")) {
                const auto* home = std::getenv("HOME");
                if (!home || !*home)
                    fail();
                path = (std::filesystem::path(home) / path.substr(2)).string();
            }
            config.receive_folder = path;
            if (!config.receive_folder.is_absolute())
                fail();
        } else if (key == "discovery") {
            if (value != "true" && value != "false")
                fail();
            config.discovery = value == "true";
        } else if (key == "listen_port" || key == "timeout_seconds") {
            unsigned parsed = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size() || !parsed ||
                parsed > (key == "listen_port" ? 65535U : 86400U))
                fail();
            if (key == "listen_port")
                config.listen_port = static_cast<std::uint16_t>(parsed);
            else
                config.timeout = std::chrono::seconds(parsed);
        } else {
            fail();
        }
    }
    return config;
}
} // namespace beam
