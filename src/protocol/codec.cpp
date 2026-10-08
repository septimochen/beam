#include "codec.hpp"
#include "beam/beam.hpp"
#include <algorithm>
#include <stdexcept>

namespace beam::protocol {
namespace {
constexpr std::size_t max_payload = 1024;
void append(std::vector<std::byte>& bytes, std::uint64_t value, unsigned count) {
    for (unsigned index = count; index > 0; --index)
        bytes.push_back(static_cast<std::byte>((value >> ((index - 1) * 8)) & 255));
}
std::uint64_t take(std::span<const std::byte>& bytes, unsigned count) {
    if (bytes.size() < count)
        throw TransferError(TransferErrorCode::protocol, "truncated protocol field");
    std::uint64_t value = 0;
    for (unsigned index = 0; index < count; ++index)
        value = (value << 8) | std::to_integer<unsigned>(bytes[index]);
    bytes = bytes.subspan(count);
    return value;
}
void validate(const Message& message) {
    if (message.id == 0)
        throw TransferError(TransferErrorCode::protocol, "zero transfer id");
    const bool failure = message.type == Type::reject || message.type == Type::failed;
    const auto code = static_cast<unsigned>(message.error);
    if ((failure && (code == 0 || code > 10)) || (!failure && code != 0))
        throw TransferError(TransferErrorCode::protocol, "invalid protocol error code");
    switch (message.type) {
    case Type::offer:
        if (!is_safe_filename(message.text))
            throw TransferError(TransferErrorCode::protocol, "unsafe offered filename");
        break;
    case Type::reject:
    case Type::failed:
        if (message.text.empty() || message.text.size() > 256)
            throw TransferError(TransferErrorCode::protocol, "invalid protocol error length");
        for (char character : message.text)
            if (static_cast<unsigned char>(character) < 32 ||
                static_cast<unsigned char>(character) > 126)
                throw TransferError(TransferErrorCode::protocol, "invalid protocol error text");
        break;
    case Type::accept:
    case Type::complete:
    case Type::acknowledged:
        if (!message.text.empty())
            throw TransferError(TransferErrorCode::protocol, "unexpected protocol text");
        break;
    default:
        throw TransferError(TransferErrorCode::protocol, "unknown protocol message type");
    }
}
} // namespace
std::vector<std::byte> encode(const Message& message) {
    validate(message);
    std::vector<std::byte> payload;
    append(payload, message.id, 8);
    if (message.type == Type::offer) {
        append(payload, message.size, 8);
        payload.insert(payload.end(), message.digest.begin(), message.digest.end());
    }
    if (message.type == Type::reject || message.type == Type::failed)
        append(payload, static_cast<std::uint16_t>(message.error), 2);
    if (message.type == Type::offer || message.type == Type::reject ||
        message.type == Type::failed) {
        append(payload, message.text.size(), 2);
        for (char character : message.text)
            payload.push_back(static_cast<std::byte>(character));
    }
    std::vector<std::byte> frame{std::byte{2}, static_cast<std::byte>(message.type)};
    append(frame, payload.size(), 4);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}
Message decode(std::span<const std::byte> bytes) {
    if (take(bytes, 1) != 2)
        throw TransferError(TransferErrorCode::protocol, "unsupported protocol version");
    Message message{};
    message.type = static_cast<Type>(take(bytes, 1));
    const auto length = take(bytes, 4);
    if (length > max_payload || bytes.size() != length)
        throw TransferError(TransferErrorCode::protocol, "invalid protocol frame length");
    message.id = take(bytes, 8);
    if (message.type == Type::offer) {
        message.size = take(bytes, 8);
        if (bytes.size() < message.digest.size())
            throw TransferError(TransferErrorCode::protocol, "truncated SHA-256 digest");
        std::copy_n(bytes.begin(), message.digest.size(), message.digest.begin());
        bytes = bytes.subspan(message.digest.size());
    }
    if (message.type == Type::offer || message.type == Type::reject ||
        message.type == Type::failed) {
        if (message.type == Type::reject || message.type == Type::failed)
            message.error = static_cast<TransferErrorCode>(take(bytes, 2));
        const auto text_length = take(bytes, 2);
        if (text_length != bytes.size())
            throw TransferError(TransferErrorCode::protocol, "invalid protocol text length");
        message.text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        bytes = {};
    }
    if (!bytes.empty())
        throw TransferError(TransferErrorCode::protocol, "trailing protocol bytes");
    validate(message);
    return message;
}
Message read(transport::Stream& stream) {
    std::array<std::byte, 6> header{};
    transport::read_exact(stream, header);
    if (header[0] != std::byte{2})
        throw TransferError(TransferErrorCode::protocol, "unsupported protocol version");
    const auto type = std::to_integer<unsigned>(header[1]);
    if (type < 1 || type > 6)
        throw TransferError(TransferErrorCode::protocol, "unknown protocol message type");
    std::span<const std::byte> length_bytes{header.data() + 2, 4};
    const auto length = take(length_bytes, 4);
    if (length < 8 || length > max_payload ||
        ((type == 2 || type == 4 || type == 6) && length != 8) ||
        (type == 1 && (length < 51 || length > 305)) ||
        ((type == 3 || type == 5) && (length < 13 || length > 268)))
        throw TransferError(TransferErrorCode::protocol, "protocol payload exceeds limit");
    std::vector<std::byte> frame(header.begin(), header.end());
    frame.resize(header.size() + static_cast<std::size_t>(length));
    transport::read_exact(stream, std::span{frame}.subspan(header.size()));
    return decode(frame);
}
void write(transport::Stream& stream, const Message& message) { stream.send(encode(message)); }
std::array<std::byte, 8> encode_id(std::uint64_t id) {
    std::array<std::byte, 8> bytes{};
    for (unsigned index = 0; index < 8; ++index)
        bytes[index] = static_cast<std::byte>((id >> ((7 - index) * 8)) & 255);
    return bytes;
}
std::uint64_t decode_id(std::span<const std::byte> bytes) {
    if (bytes.size() != 8)
        throw TransferError(TransferErrorCode::protocol, "invalid stream transfer id");
    return take(bytes, 8);
}
} // namespace beam::protocol
