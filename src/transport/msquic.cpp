#include "transport.hpp"
#include <msquic.h>
#include <openssl/pkcs7.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace beam::transport {
namespace {
const char* describe(QUIC_STATUS status) {
    switch (status) {
    case QUIC_STATUS_CONNECTION_TIMEOUT:
        return "handshake or connection timed out";
    case QUIC_STATUS_CONNECTION_IDLE:
        return "peer stopped responding";
    case QUIC_STATUS_CONNECTION_REFUSED:
        return "peer refused the connection";
    case QUIC_STATUS_UNREACHABLE:
        return "peer address is unreachable";
    case QUIC_STATUS_ADDRESS_IN_USE:
        return "listen address/port is already in use";
    case QUIC_STATUS_HANDSHAKE_FAILURE:
        return "TLS handshake failed";
    case QUIC_STATUS_TLS_ERROR:
        return "TLS credentials or peer verification failed";
    case QUIC_STATUS_BAD_CERTIFICATE:
        return "peer certificate is not authorized by the configured CA";
    case QUIC_STATUS_EXPIRED_CERTIFICATE:
        return "peer certificate has expired";
    case QUIC_STATUS_REQUIRED_CERTIFICATE:
        return "peer did not supply a required certificate";
    case QUIC_STATUS_ALPN_NEG_FAILURE:
        return "peer does not support the Beam protocol";
    default:
        return "transport operation failed";
    }
}
void checked(QUIC_STATUS status, const char* operation) {
    if (QUIC_FAILED(status)) {
        std::ostringstream message;
        message << operation << ": " << describe(status) << " (MsQuic status 0x" << std::hex
                << status << "; check peer certificates, endpoint and network)";
        throw std::runtime_error(message.str());
    }
}
struct Runtime {
    const QUIC_API_TABLE* api{};
    HQUIC registration{};
    HQUIC configuration{};
    std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)> trust{nullptr, X509_STORE_free};
    bool client;
    explicit Runtime(const TransferOptions& options, bool is_client) : client(is_client) {
        try {
            checked(MsQuicOpen2(&api), "open MsQuic");
            const QUIC_REGISTRATION_CONFIG registration_config{"beam",
                                                               QUIC_EXECUTION_PROFILE_LOW_LATENCY};
            checked(api->RegistrationOpen(&registration_config, &registration),
                    "open registration");
            std::string alpn_text = "beam/1";
            const QUIC_BUFFER alpn{static_cast<std::uint32_t>(alpn_text.size()),
                                   reinterpret_cast<std::uint8_t*>(alpn_text.data())};
            QUIC_SETTINGS settings{};
            settings.IdleTimeoutMs = static_cast<std::uint64_t>(options.timeout.count());
            settings.IsSet.IdleTimeoutMs = TRUE;
            settings.SendBufferingEnabled = FALSE;
            settings.IsSet.SendBufferingEnabled = TRUE;
            settings.PeerBidiStreamCount = 1;
            settings.IsSet.PeerBidiStreamCount = TRUE;
            settings.PeerUnidiStreamCount = 1;
            settings.IsSet.PeerUnidiStreamCount = TRUE;
            settings.StreamRecvWindowDefault = 65536;
            settings.IsSet.StreamRecvWindowDefault = TRUE;
            checked(api->ConfigurationOpen(registration, &alpn, 1, &settings, sizeof(settings),
                                           nullptr, &configuration),
                    "open configuration");
            const auto key = options.credentials.private_key.string();
            const auto certificate = options.credentials.certificate.string();
            const auto ca = options.credentials.ca_certificate.string();
            trust.reset(X509_STORE_new());
            if (!trust || X509_STORE_load_locations(trust.get(), ca.c_str(), nullptr) != 1)
                throw std::runtime_error("load dedicated peer CA trust store failed");
            QUIC_CERTIFICATE_FILE files{key.c_str(), certificate.c_str()};
            QUIC_CREDENTIAL_CONFIG credentials{};
            credentials.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
            credentials.CertificateFile = &files;
            credentials.CaCertificateFile = ca.c_str();
            credentials.Flags = static_cast<QUIC_CREDENTIAL_FLAGS>(
                QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE |
                QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED |
                QUIC_CREDENTIAL_FLAG_USE_PORTABLE_CERTIFICATES |
                QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION |
                (client ? QUIC_CREDENTIAL_FLAG_CLIENT
                        : QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION));
            checked(api->ConfigurationLoadCredential(configuration, &credentials),
                    "load TLS credentials");
        } catch (...) {
            close();
            throw;
        }
    }
    bool verify_peer(const QUIC_CONNECTION_EVENT& event) const noexcept {
        // MsQuic also loads system roots. Independently constrain authorization to
        // --ca, preserving all of MsQuic's normal TLS/hostname checks.
        const auto* certificate =
            static_cast<const QUIC_BUFFER*>(event.PEER_CERTIFICATE_RECEIVED.Certificate);
        const auto* chain = static_cast<const QUIC_BUFFER*>(event.PEER_CERTIFICATE_RECEIVED.Chain);
        if (!certificate || !certificate->Buffer || certificate->Length == 0 ||
            certificate->Length > 65536)
            return false;
        const unsigned char* cursor = certificate->Buffer;
        std::unique_ptr<X509, decltype(&X509_free)> leaf(
            d2i_X509(nullptr, &cursor, static_cast<long>(certificate->Length)), X509_free);
        if (!leaf || cursor != certificate->Buffer + certificate->Length)
            return false;
        std::unique_ptr<PKCS7, decltype(&PKCS7_free)> certificates(nullptr, PKCS7_free);
        if (chain && chain->Length) {
            if (!chain->Buffer || chain->Length > 1048576)
                return false;
            cursor = chain->Buffer;
            certificates.reset(d2i_PKCS7(nullptr, &cursor, static_cast<long>(chain->Length)));
            if (!certificates || cursor != chain->Buffer + chain->Length ||
                !PKCS7_type_is_signed(certificates.get()))
                return false;
        }
        std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> context(
            X509_STORE_CTX_new(), X509_STORE_CTX_free);
        if (!context ||
            X509_STORE_CTX_init(context.get(), trust.get(), leaf.get(),
                                certificates ? certificates->d.sign->cert : nullptr) != 1)
            return false;
        if (X509_STORE_CTX_set_purpose(context.get(), client ? X509_PURPOSE_SSL_SERVER
                                                             : X509_PURPOSE_SSL_CLIENT) != 1)
            return false;
        return X509_verify_cert(context.get()) == 1;
    }
    void close() noexcept {
        if (configuration)
            api->ConfigurationClose(configuration);
        if (registration)
            api->RegistrationClose(registration);
        if (api)
            MsQuicClose(api);
    }
    ~Runtime() { close(); }
};
class QuicStream final : public Stream {
  public:
    const QUIC_API_TABLE* api;
    HQUIC handle{};
    bool unidirectional;
    std::chrono::milliseconds timeout;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<QUIC_BUFFER> received;
    std::size_t buffer_index{}, buffer_offset{};
    std::uint64_t pending_length{};
    bool eof{}, failed{}, sent{}, send_finished{}, started{}, stopped{};
    QuicStream(const QUIC_API_TABLE* table, bool uni, std::chrono::milliseconds duration)
        : api(table), unidirectional(uni), timeout(duration) {}
    void close() noexcept {
        if (!handle)
            return;
        if (started) {
            const auto flags = static_cast<QUIC_STREAM_SHUTDOWN_FLAGS>(
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT | QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE);
            const auto status = api->StreamShutdown(handle, flags, 1);
            if (QUIC_SUCCEEDED(status)) {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stopped; });
            }
        }
        api->StreamClose(handle);
        handle = nullptr;
    }
    ~QuicStream() override { close(); }
    static QUIC_STATUS QUIC_API callback(HQUIC, void* context, QUIC_STREAM_EVENT* event) noexcept {
        auto& self = *static_cast<QuicStream*>(context);
        try {
            std::lock_guard lock(self.mutex);
            switch (event->Type) {
            case QUIC_STREAM_EVENT_RECEIVE:
                // MsQuic can indicate a FIN with zero bytes. Complete that event
                // inline so PEER_SEND_SHUTDOWN can follow without a pending read.
                if (event->RECEIVE.TotalBufferLength == 0)
                    break;
                self.received.assign(event->RECEIVE.Buffers,
                                     event->RECEIVE.Buffers + event->RECEIVE.BufferCount);
                self.pending_length = event->RECEIVE.TotalBufferLength;
                self.buffer_index = self.buffer_offset = 0;
                self.changed.notify_all();
                return QUIC_STATUS_PENDING;
            case QUIC_STREAM_EVENT_SEND_COMPLETE:
                self.sent = true;
                self.failed |= event->SEND_COMPLETE.Canceled != FALSE;
                break;
            case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
                self.eof = true;
                break;
            case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
                self.send_finished = true;
                self.failed |= event->SEND_SHUTDOWN_COMPLETE.Graceful == FALSE;
                break;
            case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
            case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
                self.failed = true;
                break;
            case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
                self.stopped = true;
                if (!self.eof && !self.send_finished)
                    self.failed = true;
                break;
            default:
                break;
            }
            self.changed.notify_all();
            return QUIC_STATUS_SUCCESS;
        } catch (...) {
            return QUIC_STATUS_OUT_OF_MEMORY;
        }
    }
    template <typename Predicate>
    void wait(std::unique_lock<std::mutex>& lock, Predicate predicate) {
        if (!changed.wait_for(lock, timeout, [&] { return failed || stopped || predicate(); }))
            throw std::runtime_error("QUIC stream timed out");
        if (failed || (stopped && !predicate()))
            throw std::runtime_error("QUIC stream interrupted or rejected by peer");
    }
    void send(std::span<const std::byte> bytes) override {
        if (bytes.empty())
            return;
        if (bytes.size() > 65536)
            throw std::runtime_error("transport send exceeds bounded buffer");
        std::unique_lock lock(mutex);
        sent = false;
        QUIC_BUFFER buffer{static_cast<std::uint32_t>(bytes.size()),
                           reinterpret_cast<std::uint8_t*>(const_cast<std::byte*>(bytes.data()))};
        checked(api->StreamSend(handle, &buffer, 1, QUIC_SEND_FLAG_NONE, nullptr), "send stream");
        try {
            wait(lock, [&] { return sent; });
        } catch (...) {
            // MsQuic may still own the caller's buffer on timeout. Closing synchronously
            // releases it and drains callbacks before unwinding the caller's storage.
            lock.unlock();
            close();
            throw;
        }
    }
    std::size_t read(std::span<std::byte> bytes) override {
        std::unique_lock lock(mutex);
        wait(lock, [&] { return pending_length != 0 || eof; });
        if (!pending_length)
            return 0;
        std::size_t copied = 0;
        while (copied < bytes.size() && buffer_index < received.size()) {
            const auto& buffer = received[buffer_index];
            const auto count = std::min(bytes.size() - copied,
                                        static_cast<std::size_t>(buffer.Length) - buffer_offset);
            std::memcpy(bytes.data() + copied, buffer.Buffer + buffer_offset, count);
            copied += count;
            buffer_offset += count;
            if (buffer_offset == buffer.Length) {
                ++buffer_index;
                buffer_offset = 0;
            }
        }
        if (buffer_index == received.size()) {
            const auto consumed = pending_length;
            pending_length = 0;
            received.clear();
            lock.unlock();
            api->StreamReceiveComplete(handle, consumed);
        }
        return copied;
    }
    void finish() override {
        std::unique_lock lock(mutex);
        checked(api->StreamShutdown(handle, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0),
                "finish stream");
        wait(lock, [&] { return send_finished; });
    }
};
class QuicConnection final : public Connection {
  public:
    Runtime runtime;
    HQUIC handle{}, listener{};
    std::chrono::milliseconds timeout;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::unique_ptr<QuicStream>> incoming;
    bool connected{}, stopped{}, failed{}, accepted{}, bidi_seen{}, uni_seen{};
    QUIC_STATUS failure_status{QUIC_STATUS_SUCCESS};
    explicit QuicConnection(const TransferOptions& options, bool client)
        : runtime(options, client), timeout(options.timeout) {}
    ~QuicConnection() override {
        if (listener)
            runtime.api->ListenerClose(listener);
        if (handle) {
            runtime.api->ConnectionShutdown(handle, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stopped; });
            }
            incoming.clear();
            runtime.api->ConnectionClose(handle);
        }
    }
    static QUIC_STATUS QUIC_API callback(HQUIC, void* context,
                                         QUIC_CONNECTION_EVENT* event) noexcept {
        auto& self = *static_cast<QuicConnection*>(context);
        try {
            std::lock_guard lock(self.mutex);
            switch (event->Type) {
            case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
                if (!self.runtime.verify_peer(*event))
                    return QUIC_STATUS_BAD_CERTIFICATE;
                break;
            case QUIC_CONNECTION_EVENT_CONNECTED:
                self.connected = true;
                break;
            case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
                self.failure_status = event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status;
                self.failed = true;
                break;
            case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
                self.failed = true;
                break;
            case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
                self.stopped = true;
                break;
            case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
                const bool uni =
                    (event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) != 0;
                auto stream = std::make_unique<QuicStream>(self.runtime.api, uni, self.timeout);
                stream->handle = event->PEER_STREAM_STARTED.Stream;
                stream->started = true;
                self.runtime.api->SetCallbackHandler(
                    stream->handle, reinterpret_cast<void*>(QuicStream::callback), stream.get());
                auto& seen = uni ? self.uni_seen : self.bidi_seen;
                if (seen) {
                    self.failed = true;
                    self.runtime.api->ConnectionShutdown(self.handle,
                                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 1);
                    // Already on the connection worker: immediate abort drains this
                    // stream's callback before its local owner is destroyed.
                } else {
                    seen = true;
                    self.incoming.push_back(std::move(stream));
                }
                break;
            }
            default:
                break;
            }
            self.changed.notify_all();
            return QUIC_STATUS_SUCCESS;
        } catch (...) {
            return QUIC_STATUS_OUT_OF_MEMORY;
        }
    }
    static QUIC_STATUS QUIC_API listener_callback(HQUIC, void* context,
                                                  QUIC_LISTENER_EVENT* event) noexcept {
        auto& self = *static_cast<QuicConnection*>(context);
        if (event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION)
            return QUIC_STATUS_SUCCESS;
        try {
            std::lock_guard lock(self.mutex);
            if (self.accepted)
                return QUIC_STATUS_CONNECTION_REFUSED;
            self.accepted = true;
            self.handle = event->NEW_CONNECTION.Connection;
            self.runtime.api->SetCallbackHandler(self.handle, reinterpret_cast<void*>(callback),
                                                 &self);
            const auto status = self.runtime.api->ConnectionSetConfiguration(
                self.handle, self.runtime.configuration);
            if (QUIC_FAILED(status)) {
                self.handle = nullptr; // Returning failure leaves ownership with MsQuic.
                self.failed = true;
                self.failure_status = status;
            }
            self.changed.notify_all();
            return status;
        } catch (...) {
            return QUIC_STATUS_INTERNAL_ERROR;
        }
    }
    template <typename Predicate>
    void wait(std::unique_lock<std::mutex>& lock, Predicate predicate) {
        if (!changed.wait_for(lock, timeout, [&] { return failed || stopped || predicate(); }))
            throw std::runtime_error("QUIC connection timed out waiting for authenticated peer");
        if (failed || stopped) {
            checked(failure_status, "QUIC handshake/connection");
            throw std::runtime_error("QUIC connection closed by peer");
        }
    }
    std::unique_ptr<Stream> open(bool uni) override {
        auto stream = std::make_unique<QuicStream>(runtime.api, uni, timeout);
        checked(runtime.api->StreamOpen(
                    handle, uni ? QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL : QUIC_STREAM_OPEN_FLAG_NONE,
                    QuicStream::callback, stream.get(), &stream->handle),
                "open stream");
        checked(runtime.api->StreamStart(stream->handle, QUIC_STREAM_START_FLAG_IMMEDIATE),
                "start stream");
        stream->started = true;
        return stream;
    }
    std::unique_ptr<Stream> accept(bool uni) override {
        std::unique_lock lock(mutex);
        wait(lock, [&] { return !incoming.empty(); });
        auto stream = std::move(incoming.front());
        incoming.pop_front();
        lock.unlock();
        if (stream->unidirectional != uni)
            throw std::runtime_error("unexpected QUIC stream direction");
        return stream;
    }
};
} // namespace
std::unique_ptr<Connection> connect(const Endpoint& endpoint, const std::string& server_name,
                                    const TransferOptions& options) {
    auto connection = std::make_unique<QuicConnection>(options, true);
    auto& api = *connection->runtime.api;
    checked(api.ConnectionOpen(connection->runtime.registration, QuicConnection::callback,
                               connection.get(), &connection->handle),
            "open connection");
    QUIC_ADDR address{};
    if (!QuicAddrFromString(endpoint.host.c_str(), endpoint.port, &address))
        throw std::runtime_error("endpoint must contain a numeric IPv4 or IPv6 address");
    checked(
        api.SetParam(connection->handle, QUIC_PARAM_CONN_REMOTE_ADDRESS, sizeof(address), &address),
        "set peer address");
    checked(api.ConnectionStart(connection->handle, connection->runtime.configuration,
                                address.Ip.sa_family, server_name.c_str(), endpoint.port),
            "connect");
    std::unique_lock lock(connection->mutex);
    connection->wait(lock, [&] { return connection->connected; });
    return connection;
}
std::unique_ptr<Connection> listen(std::uint16_t port, const TransferOptions& options) {
    auto connection = std::make_unique<QuicConnection>(options, false);
    auto& api = *connection->runtime.api;
    checked(api.ListenerOpen(connection->runtime.registration, QuicConnection::listener_callback,
                             connection.get(), &connection->listener),
            "open listener");
    QUIC_ADDR address{};
    QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&address, port);
    std::string alpn_text = "beam/1";
    const QUIC_BUFFER alpn{static_cast<std::uint32_t>(alpn_text.size()),
                           reinterpret_cast<std::uint8_t*>(alpn_text.data())};
    checked(api.ListenerStart(connection->listener, &alpn, 1, &address), "listen");
    if (options.on_listening)
        options.on_listening();
    std::unique_lock lock(connection->mutex);
    connection->wait(lock, [&] { return connection->connected; });
    return connection;
}
void read_exact(Stream& stream, std::span<std::byte> bytes) {
    while (!bytes.empty()) {
        const auto count = stream.read(bytes);
        if (!count)
            throw std::runtime_error("truncated QUIC stream");
        bytes = bytes.subspan(count);
    }
}
} // namespace beam::transport
