#pragma once
#include "transport/transport.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace beam::protocol {
using Digest = std::array<std::byte, 32>;
enum class Type : std::uint8_t {
    offer = 1,
    accept = 2,
    reject = 3,
    complete = 4,
    failed = 5,
    acknowledged = 6
};
struct Message {
    Type type;
    std::uint64_t id{};
    std::uint64_t size{};
    Digest digest{};
    std::string text;
    TransferErrorCode error{TransferErrorCode::none};
};
std::vector<std::byte> encode(const Message& message);
Message decode(std::span<const std::byte> bytes);
Message read(transport::Stream& stream);
void write(transport::Stream& stream, const Message& message);
std::array<std::byte, 8> encode_id(std::uint64_t id);
std::uint64_t decode_id(std::span<const std::byte> bytes);
} // namespace beam::protocol
