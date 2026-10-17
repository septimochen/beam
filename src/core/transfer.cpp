#include "beam/transfer.hpp"
#include "beam/beam.hpp"
#include "beam/routing.hpp"
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
    if (options.connect_timeout.count() < 0 || options.connect_timeout > options.timeout)
        throw std::invalid_argument("connect timeout must be zero or no longer than timeout");
    if (options.fallback_endpoints.size() > 3)
        throw std::invalid_argument("at most three fallback endpoints are allowed");
    for (const auto& endpoint : options.fallback_endpoints)
        static_cast<void>(numeric_endpoint(endpoint_string(endpoint)));
    if (options.credentials.certificate.empty() || options.credentials.private_key.empty() ||
        (options.credentials.ca_certificate.empty() &&
         options.credentials.pinned_certificates.empty()))
        throw std::invalid_argument("certificate, private key and trusted CA are required");
    if (!options.credentials.ca_certificate.empty() &&
        !options.credentials.pinned_certificates.empty())
        throw std::invalid_argument("CA credentials cannot be combined with paired credentials");
    transport::check_cancel(options);
}
std::unique_ptr<transport::Connection> connect_for_transfer(const Endpoint& endpoint,
                                                            const std::string& server_name,
                                                            const TransferOptions& options) {
    std::vector<Endpoint> candidates{numeric_endpoint(endpoint_string(endpoint))};
    for (const auto& fallback : options.fallback_endpoints) {
        const auto normalized = numeric_endpoint(endpoint_string(fallback));
        if (std::none_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                return endpoint_string(candidate) == endpoint_string(normalized);
            }))
            candidates.push_back(normalized);
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        transport::check_cancel(options);
        if (options.on_connecting)
            options.on_connecting(candidates[index]);
        transport::check_cancel(options);
        try {
            return transport::connect(candidates[index], server_name, options);
        } catch (const transport::ConnectFailure&) {
            if (index + 1 == candidates.size())
                throw;
        }
    }
    throw std::logic_error("missing transfer endpoint");
}
void progress(const TransferOptions& options, TransferStage stage, const std::string& filename,
              std::uint64_t bytes, std::uint64_t total, double speed = 0) {
    transport::check_cancel(options);
    if (options.on_progress)
        options.on_progress({stage, filename, bytes, total, speed});
    transport::check_cancel(options);
}
template <typename Read>
TransferResult send_offer(const Endpoint& endpoint, const std::string& server_name,
                          const TransferOptions& options, const protocol::Message& offer,
                          Read read) {
    const auto filename = offer.kind == ContentKind::file ? offer.text : "text";
    std::array<std::byte, 65536> buffer{};
    progress(options, TransferStage::connecting, filename, 0, offer.size);
    auto connection = connect_for_transfer(endpoint, server_name, options);
    // Keep streams alive through the catch so the application error reaches the
    // peer before stream teardown. The connection always outlives both streams.
    std::unique_ptr<transport::Stream> control, payload;
    try {
        control = connection->open(false);
        protocol::write(*control, offer);
        progress(options, TransferStage::waiting, filename, 0, offer.size);
        expect(protocol::read(*control), protocol::Type::accept, offer.id);
        payload = connection->open(true);
        payload->send(protocol::encode_id(offer.id));
        std::uint64_t sent = 0;
        const auto started = std::chrono::steady_clock::now();
        progress(options, TransferStage::transferring, filename, 0, offer.size);
        for (;;) {
            transport::check_cancel(options);
            const auto count = read(buffer);
            if (!count)
                break;
            if (count > offer.size - sent)
                throw TransferError(TransferErrorCode::source_changed,
                                    "input file grew during transfer");
            payload->send(std::span{buffer}.first(count));
            sent += count;
            const auto seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            progress(options, TransferStage::transferring, filename, sent, offer.size,
                     seconds > 0 ? static_cast<double>(sent) / seconds : 0);
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
        return {filename, sent, offer.kind};
    } catch (const TransferError& error) {
        connection->abort(error.code);
        throw;
    } catch (...) {
        connection->abort(TransferErrorCode::io);
        throw;
    }
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
    return send_offer(endpoint, server_name, options, offer,
                      [&](std::span<std::byte> bytes) { return source.read(bytes); });
}
namespace {
TransferResult send_memory(const Endpoint& endpoint, std::string_view content, ContentKind kind,
                           const std::string& server_name, const TransferOptions& options) {
    validate_content(kind, content);
    // Snapshot caller storage before invoking callbacks; allocation is bounded.
    const std::string snapshot(content);
    validate_options(options);
    if (endpoint.port == 0 || server_name.empty())
        throw std::invalid_argument("peer port and TLS server name are required");
    const auto bytes = std::as_bytes(std::span{snapshot.data(), snapshot.size()});
    const std::string label = "text";
    progress(options, TransferStage::hashing, label, 0, bytes.size());
    transfer::Hash hash;
    hash.update(bytes);
    progress(options, TransferStage::hashing, label, bytes.size(), bytes.size());
    protocol::Message offer{protocol::Type::offer,
                            transfer::random_id(),
                            bytes.size(),
                            hash.finish(),
                            {},
                            TransferErrorCode::none,
                            kind};
    std::size_t offset = 0;
    return send_offer(endpoint, server_name, options, offer, [&](std::span<std::byte> output) {
        const auto count = std::min(output.size(), bytes.size() - offset);
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), count, output.begin());
        offset += count;
        return count;
    });
}
TransferResult receive(std::uint16_t port, const std::filesystem::path& directory,
                       const TransferOptions& options, bool file_only) {
    validate_options(options);
    if (!port)
        throw std::invalid_argument("listen port must be between 1 and 65535");
    std::unique_ptr<transfer::ReceiveDirectory> output;
    if (!directory.empty())
        output = std::make_unique<transfer::ReceiveDirectory>(directory);
    progress(options, TransferStage::waiting, {}, 0, 0);
    auto connection = transport::listen(port, options);
    std::unique_ptr<transport::Stream> control, payload;
    protocol::Message offer{};
    bool accepted = false;
    try {
        control = connection->accept(false);
        offer = protocol::read(*control);
        if (offer.type == protocol::Type::ping) {
            std::array<std::byte, 1> trailing{};
            if (control->read(trailing) != 0)
                throw TransferError(TransferErrorCode::protocol, "unexpected probe bytes");
            protocol::write(*control, {protocol::Type::pong, offer.id, 0, {}, {}});
            control->finish();
            TransferResult result;
            result.connectivity_check = true;
            return result;
        }
        if (offer.type != protocol::Type::offer)
            throw TransferError(TransferErrorCode::protocol, "expected transfer offer");
        if (file_only && offer.kind != ContentKind::file)
            throw TransferError(TransferErrorCode::rejected, "receiver accepts only files");
        const auto filename = offer.kind == ContentKind::file ? offer.text : "text";
        std::unique_ptr<transfer::Destination> destination;
        if (offer.kind == ContentKind::file) {
            if (!output)
                throw TransferError(TransferErrorCode::io, "choose a receive directory for files");
            destination = std::make_unique<transfer::Destination>(*output, filename);
        }
        std::string content;
        if (offer.kind != ContentKind::file)
            content.reserve(static_cast<std::size_t>(offer.size));
        protocol::write(*control, {protocol::Type::accept, offer.id, 0, {}, {}});
        accepted = true;
        progress(options, TransferStage::transferring, filename, 0, offer.size);
        payload = connection->accept(true);
        std::array<std::byte, 8> id{};
        transport::read_exact(*payload, id);
        if (protocol::decode_id(id) != offer.id)
            throw TransferError(TransferErrorCode::protocol, "payload transfer id mismatch");
        transfer::Hash hash;
        std::array<std::byte, 65536> buffer{};
        std::uint64_t received = 0;
        const auto started = std::chrono::steady_clock::now();
        for (;;) {
            const auto count = payload->read(buffer);
            if (!count)
                break;
            if (count > offer.size - received)
                throw TransferError(TransferErrorCode::size, "payload exceeds offered size");
            const auto bytes = std::span{buffer}.first(count);
            if (destination)
                destination->write(bytes);
            if (offer.kind != ContentKind::file)
                content.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            hash.update(bytes);
            received += count;
            const auto seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            progress(options, TransferStage::transferring, filename, received, offer.size,
                     seconds > 0 ? static_cast<double>(received) / seconds : 0);
        }
        if (received != offer.size)
            throw TransferError(TransferErrorCode::size, "payload smaller than offered size");
        progress(options, TransferStage::verifying, filename, received, offer.size);
        if (hash.finish() != offer.digest)
            throw TransferError(TransferErrorCode::integrity,
                                "SHA-256 integrity verification failed");
        if (offer.kind != ContentKind::file) {
            try {
                validate_content(offer.kind, content);
            } catch (const std::invalid_argument&) {
                throw TransferError(TransferErrorCode::protocol, "invalid received text content");
            }
        }
        transport::check_cancel(options);
        if (destination)
            destination->commit();
        protocol::write(*control, {protocol::Type::complete, offer.id, 0, {}, {}});
        expect(protocol::read(*control), protocol::Type::acknowledged, offer.id);
        std::array<std::byte, 1> trailing{};
        if (control->read(trailing) != 0)
            throw TransferError(TransferErrorCode::protocol,
                                "unexpected bytes after acknowledgement");
        control->finish();
        if (options.on_progress)
            options.on_progress({TransferStage::complete, filename, received, offer.size});
        return {filename, received, offer.kind, std::move(content)};
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
} // namespace
TransferResult send_text(const Endpoint& endpoint, std::string_view text,
                         const std::string& server_name, const TransferOptions& options) {
    return send_memory(endpoint, text, ContentKind::text, server_name, options);
}
void check_connectivity(const Endpoint& endpoint, const std::string& server_name,
                        const TransferOptions& options) {
    validate_options(options);
    if (!endpoint.port || server_name.empty())
        throw std::invalid_argument("peer port and TLS server name are required");
    auto connection = connect_for_transfer(endpoint, server_name, options);
    try {
        const auto id = transfer::random_id();
        auto control = connection->open(false);
        protocol::write(*control, {protocol::Type::ping, id, 0, {}, {}});
        control->finish();
        expect(protocol::read(*control), protocol::Type::pong, id);
        std::array<std::byte, 1> trailing{};
        if (control->read(trailing) != 0)
            throw TransferError(TransferErrorCode::protocol, "unexpected probe response bytes");
    } catch (const TransferError& error) {
        connection->abort(error.code);
        throw;
    } catch (...) {
        connection->abort(TransferErrorCode::io);
        throw;
    }
}
TransferResult receive_content(std::uint16_t port, const std::filesystem::path& directory,
                               const TransferOptions& options) {
    return receive(port, directory, options, false);
}
TransferResult receive_file(std::uint16_t port, const std::filesystem::path& directory,
                            const TransferOptions& options) {
    return receive(port, directory, options, true);
}
} // namespace beam
