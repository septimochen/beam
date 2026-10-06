#include "beam/transfer.hpp"
#include "beam/beam.hpp"
#include "protocol/codec.hpp"
#include "transfer/files.hpp"
#include "transport/transport.hpp"
#include <array>
#include <charconv>
#include <stdexcept>

namespace beam {
Endpoint parse_endpoint(const std::string& endpoint) {
    const auto separator = endpoint.rfind(':');
    if (separator == std::string::npos || separator == 0)
        throw std::runtime_error("expected IP:port or [IPv6]:port");
    auto host = endpoint.substr(0, separator);
    if (host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    else if (host.find(':') != std::string::npos)
        throw std::runtime_error("IPv6 endpoints require brackets");
    unsigned port = 0;
    const auto text = std::string_view{endpoint}.substr(separator + 1);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (host.empty() || error != std::errc{} || end != text.data() + text.size() || port == 0 ||
        port > 65535)
        throw std::runtime_error("invalid endpoint host or port");
    return {host, static_cast<std::uint16_t>(port)};
}
namespace {
void expect(const protocol::Message& message, protocol::Type type, std::uint64_t id) {
    if (message.id != id)
        throw std::runtime_error("unexpected transfer id");
    if (message.type == protocol::Type::reject || message.type == protocol::Type::failed)
        throw std::runtime_error("peer refused transfer: " + message.text);
    if (message.type != type)
        throw std::runtime_error("unexpected transfer state/message");
}
void validate_options(const TransferOptions& options) {
    if (options.timeout.count() <= 0 || options.timeout > std::chrono::hours(24))
        throw std::runtime_error("timeout must be positive and no longer than 24 hours");
    if (options.credentials.certificate.empty() || options.credentials.private_key.empty() ||
        options.credentials.ca_certificate.empty())
        throw std::runtime_error("certificate, private key and trusted CA are required");
}
} // namespace
TransferResult send_file(const Endpoint& endpoint, const std::filesystem::path& path,
                         const std::string& server_name, const TransferOptions& options) {
    validate_options(options);
    if (endpoint.port == 0 || server_name.empty())
        throw std::runtime_error("peer port and TLS server name are required");
    const auto filename = path.filename().string();
    if (!is_safe_filename(filename))
        throw std::runtime_error("input filename is not a safe portable basename");
    transfer::Source source(path);
    transfer::Hash hash;
    std::array<std::byte, 65536> buffer{};
    std::uint64_t hashed_size = 0;
    for (;;) {
        const auto count = source.read(buffer);
        if (!count)
            break;
        hash.update(std::span{buffer}.first(count));
        hashed_size += count;
    }
    if (hashed_size != source.size)
        throw std::runtime_error("input file changed while hashing");
    protocol::Message offer{protocol::Type::offer, transfer::random_id(), source.size,
                            hash.finish(), filename};
    source.rewind();
    auto connection = transport::connect(endpoint, server_name, options);
    auto control = connection->open(false);
    protocol::write(*control, offer);
    expect(protocol::read(*control), protocol::Type::accept, offer.id);
    auto payload = connection->open(true);
    payload->send(protocol::encode_id(offer.id));
    std::uint64_t sent = 0;
    for (;;) {
        const auto count = source.read(buffer);
        if (!count)
            break;
        if (count > offer.size - sent)
            throw std::runtime_error("input file grew during transfer");
        payload->send(std::span{buffer}.first(count));
        sent += count;
    }
    if (sent != offer.size)
        throw std::runtime_error("input file shrank during transfer");
    payload->finish();
    expect(protocol::read(*control), protocol::Type::complete, offer.id);
    protocol::write(*control, {protocol::Type::acknowledged, offer.id, 0, {}, {}});
    control->finish();
    std::array<std::byte, 1> trailing{};
    if (control->read(trailing) != 0)
        throw std::runtime_error("unexpected bytes after completion");
    return {filename, sent};
}
TransferResult receive_file(std::uint16_t port, const std::filesystem::path& directory,
                            const TransferOptions& options) {
    validate_options(options);
    if (!port)
        throw std::runtime_error("listen port must be between 1 and 65535");
    transfer::ReceiveDirectory output(directory);
    auto connection = transport::listen(port, options);
    auto control = connection->accept(false);
    const auto offer = protocol::read(*control);
    if (offer.type != protocol::Type::offer)
        throw std::runtime_error("expected transfer offer");
    bool accepted = false;
    try {
        transfer::Destination destination(output, offer.text);
        protocol::write(*control, {protocol::Type::accept, offer.id, 0, {}, {}});
        accepted = true;
        auto payload = connection->accept(true);
        std::array<std::byte, 8> id{};
        transport::read_exact(*payload, id);
        if (protocol::decode_id(id) != offer.id)
            throw std::runtime_error("payload transfer id mismatch");
        transfer::Hash hash;
        std::array<std::byte, 65536> buffer{};
        std::uint64_t received = 0;
        for (;;) {
            const auto count = payload->read(buffer);
            if (!count)
                break;
            if (count > offer.size - received)
                throw std::runtime_error("payload exceeds offered size");
            const auto bytes = std::span{buffer}.first(count);
            destination.write(bytes);
            hash.update(bytes);
            received += count;
        }
        if (received != offer.size)
            throw std::runtime_error("payload smaller than offered size");
        if (hash.finish() != offer.digest)
            throw std::runtime_error("SHA-256 integrity verification failed");
        destination.commit();
        protocol::write(*control, {protocol::Type::complete, offer.id, 0, {}, {}});
        expect(protocol::read(*control), protocol::Type::acknowledged, offer.id);
        std::array<std::byte, 1> trailing{};
        if (control->read(trailing) != 0)
            throw std::runtime_error("unexpected bytes after acknowledgement");
        control->finish();
        return {offer.text, received};
    } catch (...) {
        try {
            protocol::write(*control, {accepted ? protocol::Type::failed : protocol::Type::reject,
                                       offer.id,
                                       0,
                                       {},
                                       accepted ? "receive failed" : "destination unavailable"});
            control->finish();
        } catch (...) {
        }
        throw;
    }
}
} // namespace beam
