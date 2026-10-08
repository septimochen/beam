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
        throw TransferError(TransferErrorCode::protocol, "unexpected transfer id");
    if (message.type == protocol::Type::reject || message.type == protocol::Type::failed)
        throw TransferError(message.error, std::string("peer: ") + error_name(message.error), true);
    if (message.type != type)
        throw TransferError(TransferErrorCode::protocol, "unexpected transfer state/message");
}
void validate_options(const TransferOptions& options) {
    if (options.timeout.count() <= 0 || options.timeout > std::chrono::hours(24))
        throw std::invalid_argument("timeout must be positive and no longer than 24 hours");
    if (options.credentials.certificate.empty() || options.credentials.private_key.empty() ||
        options.credentials.ca_certificate.empty())
        throw std::invalid_argument("certificate, private key and trusted CA are required");
    transport::check_cancel(options);
}
void progress(const TransferOptions& options, TransferStage stage, const std::string& filename,
              std::uint64_t bytes, std::uint64_t total) {
    transport::check_cancel(options);
    if (options.on_progress)
        options.on_progress({stage, filename, bytes, total});
    transport::check_cancel(options);
}
} // namespace
TransferResult send_file(const Endpoint& endpoint, const std::filesystem::path& path,
                         const std::string& server_name, const TransferOptions& options) {
    validate_options(options);
    if (endpoint.port == 0 || server_name.empty())
        throw std::invalid_argument("peer port and TLS server name are required");
    const auto filename = path.filename().string();
    if (!is_safe_filename(filename))
        throw TransferError(TransferErrorCode::io,
                            "input filename is not a safe portable basename");
    transfer::Source source(path);
    transfer::Hash hash;
    std::array<std::byte, 65536> buffer{};
    std::uint64_t hashed_size = 0;
    progress(options, TransferStage::hashing, filename, 0, source.size);
    for (;;) {
        transport::check_cancel(options);
        const auto count = source.read(buffer);
        if (!count)
            break;
        if (count > source.size - hashed_size)
            throw TransferError(TransferErrorCode::source_changed, "input file grew while hashing");
        hash.update(std::span{buffer}.first(count));
        hashed_size += count;
        progress(options, TransferStage::hashing, filename, hashed_size, source.size);
    }
    if (hashed_size != source.size)
        throw TransferError(TransferErrorCode::source_changed, "input file changed while hashing");
    protocol::Message offer{protocol::Type::offer, transfer::random_id(), source.size,
                            hash.finish(), filename};
    source.rewind();
    progress(options, TransferStage::connecting, filename, 0, source.size);
    auto connection = transport::connect(endpoint, server_name, options);
    // Keep streams alive through the catch so the application error reaches the
    // peer before stream teardown. The connection always outlives both streams.
    std::unique_ptr<transport::Stream> control, payload;
    try {
        control = connection->open(false);
        protocol::write(*control, offer);
        progress(options, TransferStage::waiting, filename, 0, source.size);
        expect(protocol::read(*control), protocol::Type::accept, offer.id);
        payload = connection->open(true);
        payload->send(protocol::encode_id(offer.id));
        std::uint64_t sent = 0;
        progress(options, TransferStage::transferring, filename, 0, offer.size);
        for (;;) {
            transport::check_cancel(options);
            const auto count = source.read(buffer);
            if (!count)
                break;
            if (count > offer.size - sent)
                throw TransferError(TransferErrorCode::source_changed,
                                    "input file grew during transfer");
            payload->send(std::span{buffer}.first(count));
            sent += count;
            progress(options, TransferStage::transferring, filename, sent, offer.size);
        }
        if (sent != offer.size)
            throw TransferError(TransferErrorCode::source_changed,
                                "input file shrank during transfer");
        payload->finish();
        progress(options, TransferStage::verifying, filename, sent, offer.size);
        expect(protocol::read(*control), protocol::Type::complete, offer.id);
        protocol::write(*control, {protocol::Type::acknowledged, offer.id, 0, {}, {}});
        control->finish();
        std::array<std::byte, 1> trailing{};
        if (control->read(trailing) != 0)
            throw TransferError(TransferErrorCode::protocol, "unexpected bytes after completion");
        // Completion is observational: a late stop request cannot undo success.
        if (options.on_progress)
            options.on_progress({TransferStage::complete, filename, sent, offer.size});
        return {filename, sent};
    } catch (const TransferError& error) {
        connection->abort(error.code);
        throw;
    } catch (...) {
        connection->abort(TransferErrorCode::io);
        throw;
    }
}
TransferResult receive_file(std::uint16_t port, const std::filesystem::path& directory,
                            const TransferOptions& options) {
    validate_options(options);
    if (!port)
        throw std::invalid_argument("listen port must be between 1 and 65535");
    transfer::ReceiveDirectory output(directory);
    progress(options, TransferStage::waiting, {}, 0, 0);
    auto connection = transport::listen(port, options);
    std::unique_ptr<transport::Stream> control, payload;
    protocol::Message offer{};
    bool accepted = false;
    try {
        control = connection->accept(false);
        offer = protocol::read(*control);
        if (offer.type != protocol::Type::offer)
            throw TransferError(TransferErrorCode::protocol, "expected transfer offer");
        transfer::Destination destination(output, offer.text);
        protocol::write(*control, {protocol::Type::accept, offer.id, 0, {}, {}});
        accepted = true;
        progress(options, TransferStage::transferring, offer.text, 0, offer.size);
        payload = connection->accept(true);
        std::array<std::byte, 8> id{};
        transport::read_exact(*payload, id);
        if (protocol::decode_id(id) != offer.id)
            throw TransferError(TransferErrorCode::protocol, "payload transfer id mismatch");
        transfer::Hash hash;
        std::array<std::byte, 65536> buffer{};
        std::uint64_t received = 0;
        for (;;) {
            const auto count = payload->read(buffer);
            if (!count)
                break;
            if (count > offer.size - received)
                throw TransferError(TransferErrorCode::size, "payload exceeds offered size");
            const auto bytes = std::span{buffer}.first(count);
            destination.write(bytes);
            hash.update(bytes);
            received += count;
            progress(options, TransferStage::transferring, offer.text, received, offer.size);
        }
        if (received != offer.size)
            throw TransferError(TransferErrorCode::size, "payload smaller than offered size");
        progress(options, TransferStage::verifying, offer.text, received, offer.size);
        if (hash.finish() != offer.digest)
            throw TransferError(TransferErrorCode::integrity,
                                "SHA-256 integrity verification failed");
        transport::check_cancel(options);
        destination.commit();
        protocol::write(*control, {protocol::Type::complete, offer.id, 0, {}, {}});
        expect(protocol::read(*control), protocol::Type::acknowledged, offer.id);
        std::array<std::byte, 1> trailing{};
        if (control->read(trailing) != 0)
            throw TransferError(TransferErrorCode::protocol,
                                "unexpected bytes after acknowledgement");
        control->finish();
        if (options.on_progress)
            options.on_progress({TransferStage::complete, offer.text, received, offer.size});
        return {offer.text, received};
    } catch (const TransferError& error) {
        // Pre-acceptance failures can use the control response. After acceptance,
        // close with the same bounded code so even a blocked payload sender wakes.
        if (!accepted && offer.id && control && !error.remote &&
            error.code != TransferErrorCode::cancelled) {
            try {
                protocol::write(
                    *control,
                    {protocol::Type::reject, offer.id, 0, {}, error_name(error.code), error.code});
                control->finish();
            } catch (...) {
            }
        }
        connection->abort(error.code);
        throw;
    } catch (...) {
        connection->abort(TransferErrorCode::io);
        throw;
    }
}
} // namespace beam
