#include "beam/pairing.hpp"
#include "beam/routing.hpp"
#ifdef BEAM_HAS_DISCOVERY
#include "beam/discovery.hpp"
#endif
#include "protocol/pairing.hpp"
#include "transport/pairing_tls.hpp"
#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <mutex>
#include <openssl/rand.h>
#include <thread>

namespace beam {
namespace {
namespace wire = pairing_protocol;
namespace net = pairing_transport;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void validate_name(const std::string& name) {
    require(!name.empty() && name.size() <= 128 &&
                name.find_first_of("\n\r\t") == std::string::npos,
            "Device name must be one line, 1-128 UTF-8 bytes");
    validate_content(ContentKind::text, name);
}
std::string generate_pin() {
    std::uint32_t value = 0;
    do {
        require(RAND_bytes(reinterpret_cast<unsigned char*>(&value), sizeof(value)) == 1,
                "Generate pairing PIN failed");
    } while (value >= 4294000000U);
    const auto number = std::to_string(value % 1000000U);
    return std::string(6 - number.size(), '0') + number;
}
std::uint16_t port_number(const std::string& text) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    require(error == std::errc{} && end == text.data() + text.size() && value > 0 && value <= 65535,
            "Invalid peer transfer port");
    return static_cast<std::uint16_t>(value);
}
} // namespace
const char* pairing_state_name(PairingState state) noexcept {
    switch (state) {
    case PairingState::idle:
        return "idle";
    case PairingState::hosting:
        return "hosting";
    case PairingState::connecting:
        return "connecting";
    case PairingState::verifying:
        return "verifying";
    case PairingState::approval:
        return "awaiting approval";
    case PairingState::saving:
        return "saving";
    case PairingState::paired:
        return "paired";
    case PairingState::expired:
        return "expired";
    case PairingState::rejected:
        return "rejected";
    case PairingState::incomplete:
        return "incomplete";
    case PairingState::failed:
        return "failed";
    case PairingState::cancelled:
        return "cancelled";
    case PairingState::waiting:
        return "awaiting peer approval";
    }
    return "failed";
}
struct PairingSession::Impl {
    PairingOptions options;
    mutable std::mutex mutex;
    std::condition_variable changed;
    PairingSnapshot value;
    std::string approval_alias;
    bool rejected = false;
    std::jthread worker, browser;
    explicit Impl(PairingOptions configuration) : options(std::move(configuration)) {
        validate_name(options.device_name);
        require(options.transfer_port != 0 &&
                    options.lifetime > std::chrono::milliseconds::zero() &&
                    options.lifetime <= std::chrono::minutes(3),
                "Invalid pairing lifetime or transfer port");
        IdentityStore store(options.state_directory, true);
        store.initialize();
        if (store.has_incomplete_pairing()) {
            value.state = PairingState::incomplete;
            value.message = "Incomplete pairing: retry with the same device and alias, or forget "
                            "the pending approval.";
        }
    }
    void publish(PairingState state, std::string message) {
        std::lock_guard lock(mutex);
        value.state = state;
        value.message = std::move(message);
        if (state != PairingState::hosting) {
            value.invitation.clear();
            value.pin.clear();
            value.qr.clear();
        }
    }
    void failure(const std::exception& error, std::stop_token stop) {
        bool pending = false;
        try {
            pending = IdentityStore(options.state_directory).has_incomplete_pairing();
        } catch (...) { /* Preserve the original error; no trust is added here. */
        }
        const auto* transfer = dynamic_cast<const TransferError*>(&error);
        const auto state =
            pending                                                     ? PairingState::incomplete
            : stop.stop_requested()                                     ? PairingState::cancelled
            : transfer && transfer->code == TransferErrorCode::timeout  ? PairingState::expired
            : transfer && transfer->code == TransferErrorCode::rejected ? PairingState::rejected
                                                                        : PairingState::failed;
        publish(state, pending ? "Pairing incomplete. Retry with the same device and alias. " +
                                     std::string(error.what())
                               : error.what());
    }
    void stop() {
        worker.request_stop();
        browser.request_stop();
        changed.notify_all();
        if (worker.joinable())
            worker.join();
        if (browser.joinable())
            browser.join();
    }
    void reset() {
        stop();
        std::lock_guard lock(mutex);
        value = {};
        approval_alias.clear();
        rejected = false;
    }
    std::string approval(const std::string& name, const DeviceIdentity& remote,
                         std::stop_token stop, net::Deadline deadline, net::Tls& tls,
                         bool& remote_approved) {
        auto alias = wire::suggested_alias(name, IdentityStore(options.state_directory).peers(),
                                           remote.fingerprint);
        {
            std::lock_guard lock(mutex);
            value.state = PairingState::approval;
            value.message = "Review this device and approve on both screens.";
            value.remote_name = name;
            value.alias = alias;
        }
        std::unique_lock lock(mutex);
        while (approval_alias.empty()) {
            net::check(stop, deadline);
            if (rejected)
                throw TransferError(TransferErrorCode::rejected, "Pairing rejected locally");
            if (tls.readable()) {
                lock.unlock();
                const auto answer = tls.read();
                if (answer == std::vector<std::string>{"reject"})
                    throw TransferError(TransferErrorCode::rejected, "Peer rejected pairing", true);
                require(!remote_approved && answer == std::vector<std::string>{"approve"},
                        "Invalid pairing approval");
                remote_approved = true;
                lock.lock();
            }
            changed.wait_for(lock, std::chrono::milliseconds(50));
        }
        return approval_alias;
    }
    void finish(net::Tls& tls, const std::vector<std::string>& fields, std::stop_token stop,
                net::Deadline deadline) {
        require(fields.size() == 4 && fields[0] == "identity", "Invalid pairing identity frame");
        validate_name(fields[1]);
        const auto remote = IdentityStore::validate_public_certificate(fields[2]);
        const auto transfer_port = port_number(fields[3]);
        std::string alias;
        bool remote_approved = false;
        try {
            alias = approval(fields[1], remote, stop, deadline, tls, remote_approved);
        } catch (const TransferError& error) {
            if (error.code == TransferErrorCode::rejected && !error.remote)
                tls.send({"reject"});
            throw;
        }
        tls.send({"approve"});
        const auto approved = remote_approved ? std::vector<std::string>{"approve"} : tls.read();
        if (approved == std::vector<std::string>{"reject"})
            throw TransferError(TransferErrorCode::rejected, "Peer rejected pairing", true);
        require(approved == std::vector<std::string>{"approve"}, "Invalid pairing approval");
        net::check(stop, deadline);
        publish(PairingState::saving, "Saving reciprocal trust…");
        {
            IdentityStore store(options.state_directory);
            store.prepare_pairing(
                {alias, endpoint_string({tls.remote_host(), transfer_port}), remote});
        }
        tls.send({"prepared"});
        require(tls.read() == std::vector<std::string>{"prepared"}, "Peer did not prepare pairing");
        static_cast<void>(IdentityStore(options.state_directory).commit_pairing());
        tls.send({"stored"});
        require(tls.read() == std::vector<std::string>{"stored"}, "Peer did not save pairing");
        tls.send({"complete"});
        require(tls.read() == std::vector<std::string>{"complete"},
                "Peer did not acknowledge pairing");
        IdentityStore(options.state_directory).finish_pairing();
        {
            std::lock_guard lock(mutex);
            value.alias = alias;
        }
        publish(PairingState::paired, "Paired with " + fields[1] + ".");
    }
    void exchange(net::Tls& bootstrap, bool server, net::Listener* listener,
                  const Endpoint& endpoint, const std::string& expected_fingerprint,
                  std::stop_token stop, net::Deadline deadline) {
        const auto local = IdentityStore(options.state_directory).identity();
        const auto session_id = wire::random_hex(16);
        bootstrap.send({"identity", options.device_name, local.certificate_pem,
                        std::to_string(options.transfer_port), session_id});
        auto fields = bootstrap.read();
        require(fields.size() == 5 && fields[0] == "identity" && wire::hex_string(fields[4], 32),
                "Invalid pairing identity frame");
        validate_name(fields[1]);
        static_cast<void>(port_number(fields[3]));
        const auto remote = IdentityStore::validate_public_certificate(fields[2]);
        require(remote.fingerprint != local.fingerprint, "Cannot pair this device with itself");
        require(expected_fingerprint.empty() || remote.fingerprint == expected_fingerprint,
                "Invitation host certificate mismatch");
        const auto proof_id = server ? session_id : fields[4];
        fields.resize(4);
        publish(PairingState::verifying, "Verifying possession of both device keys…");
        auto credentials = IdentityStore(options.state_directory).pairing_credentials(remote);
        const auto proof_deadline =
            std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(15));
        net::Socket socket;
        if (server) {
            require(listener != nullptr, "Missing pairing listener");
            socket = listener->accept(stop, proof_deadline);
            require(net::read_hello(socket, stop, proof_deadline) ==
                        std::vector<std::string>{"beam-pair/1", "proof", proof_id},
                    "Invalid key-proof session");
        } else {
            socket = net::connect(endpoint, stop, proof_deadline);
            net::send_hello(socket, {"beam-pair/1", "proof", proof_id}, stop, proof_deadline);
        }
        net::Tls proof(std::move(socket), server, {}, credentials, remote, stop, deadline);
        // The authenticated identity data is repeated on the certificate-bound
        // connection, preventing approvals from crossing concurrent sessions.
        proof.send({"identity", options.device_name, local.certificate_pem,
                    std::to_string(options.transfer_port)});
        require(proof.read() == fields, "Pairing identity changed during key proof");
        finish(proof, fields, stop, deadline);
    }
    void host(std::stop_token stop) {
        const auto deadline = std::chrono::steady_clock::now() + options.lifetime;
        net::Listener listener(options.listen_port);
        const auto identity = IdentityStore(options.state_directory).identity();
        wire::Invitation invitation{
            wire::random_hex(16), identity.fingerprint, wire::random_hex(32), {}};
        for (const auto& host :
             options.addresses.empty() ? net::local_addresses() : options.addresses)
            invitation.endpoints.push_back(
                numeric_endpoint(endpoint_string({host, listener.port})));
        require(!invitation.endpoints.empty() && invitation.endpoints.size() <= 8,
                "Invalid pairing addresses");
        const auto encoded = wire::encode(invitation);
        const auto pin = generate_pin();
        {
            std::lock_guard lock(mutex);
            value.state = PairingState::hosting;
            value.message = "Scan QR, copy the invitation, or enter this PIN on the nearby device. "
                            "Expires in three minutes.";
            value.invitation = encoded;
            value.pin = pin;
            value.port = listener.port;
            value.qr = pairing_qr(encoded);
        }
#ifdef BEAM_HAS_DISCOVERY
        std::unique_ptr<ReceiverAdvertisement> advertisement;
        try {
            DiscoveryOptions discovery;
            discovery.pairing = true;
            discovery.stop_token = stop;
            advertisement = std::make_unique<ReceiverAdvertisement>(
                listener.port,
                is_discovery_name(options.device_name) ? options.device_name : "Beam device",
                discovery);
        } catch (const std::exception&) {
            std::lock_guard lock(mutex);
            value.message += " Nearby discovery unavailable; use QR or paste.";
        }
#endif
        unsigned failures = 0;
        while (failures < 5) {
            auto socket = listener.accept(stop, deadline);
            bool authenticated = false;
            try {
                const auto handshake_deadline =
                    std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(10));
                const auto hello = net::read_hello(socket, stop, handshake_deadline);
                require(hello.size() == 3 && hello[0] == "beam-pair/1" &&
                            ((hello[1] == "invitation" && hello[2] == invitation.id) ||
                             (hello[1] == "pin" && hello[2].empty())),
                        "Invalid or expired pairing request");
                net::Tls bootstrap(std::move(socket), true,
                                   hello[1] == "pin" ? pin : invitation.secret, {}, {}, stop,
                                   handshake_deadline);
                authenticated = true;
#ifdef BEAM_HAS_DISCOVERY
                advertisement.reset();
#endif
                publish(PairingState::verifying, "Authenticated pairing invitation.");
                exchange(bootstrap, true, &listener, {}, {}, stop, deadline);
                return;
            } catch (const std::exception&) {
                if (authenticated || stop.stop_requested())
                    throw;
                ++failures;
                {
                    std::lock_guard lock(mutex);
                    value.message = "Pairing authentication failed (" + std::to_string(failures) +
                                    "/5). Check the PIN and retry.";
                }
                // Rate-limit all attempts, including malformed hellos, without
                // blocking cancellation or extending the invitation deadline.
                const auto retry =
                    std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(1));
                while (std::chrono::steady_clock::now() < retry) {
                    net::check(stop, deadline);
                    std::unique_lock lock(mutex);
                    changed.wait_for(lock, std::chrono::milliseconds(50));
                }
            }
        }
        throw std::runtime_error(
            "Pairing window closed after five failed authentications. Open Add device again.");
    }
    void join(wire::Invitation invitation, bool pin, std::stop_token stop) {
        const auto deadline = std::chrono::steady_clock::now() + options.lifetime;
        // Retry only connectivity before authentication. Never replay an
        // exchange after a PAKE handshake or approval has started.
        net::Socket socket;
        Endpoint selected{};
        for (const auto& endpoint : invitation.endpoints) {
            try {
                socket = net::connect(
                    endpoint, stop,
                    std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(3)));
                selected = endpoint;
                break;
            } catch (const std::exception&) {
                if (stop.stop_requested())
                    throw;
            }
        }
        require(socket.fd >= 0, "Pairing host unreachable. Both devices must be on the same LAN.");
        const auto handshake_deadline =
            std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(10));
        net::send_hello(socket, {"beam-pair/1", pin ? "pin" : "invitation", invitation.id}, stop,
                        handshake_deadline);
        net::Tls bootstrap(std::move(socket), false, invitation.secret, {}, {}, stop,
                           handshake_deadline);
        exchange(bootstrap, false, nullptr, selected, invitation.fingerprint, stop, deadline);
    }
};
PairingSession::PairingSession(PairingOptions options)
    : impl(std::make_unique<Impl>(std::move(options))) {}
PairingSession::~PairingSession() { impl->stop(); }
void PairingSession::host() {
    impl->reset();
    impl->worker = std::jthread([this](std::stop_token stop) {
        try {
            impl->host(stop);
        } catch (const std::exception& error) {
            impl->failure(error, stop);
        } catch (...) {
            impl->publish(PairingState::failed, "Pairing failed");
        }
    });
}
void PairingSession::join(const std::string& text) {
    auto invitation = wire::decode(text);
    impl->reset();
    impl->publish(PairingState::connecting, "Connecting to pairing host…");
    impl->worker =
        std::jthread([this, invitation = std::move(invitation)](std::stop_token stop) mutable {
            try {
                impl->join(std::move(invitation), false, stop);
            } catch (const std::exception& error) {
                impl->failure(error, stop);
            } catch (...) {
                impl->publish(PairingState::failed, "Pairing failed");
            }
        });
}
void PairingSession::join_pin(const std::string& endpoint, const std::string& pin) {
    require(pin.size() == 6 &&
                std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; }),
            "PIN must contain exactly six digits");
    wire::Invitation invitation{{}, {}, pin, {numeric_endpoint(endpoint)}};
    impl->reset();
    impl->publish(PairingState::connecting, "Authenticating the nearby device…");
    impl->worker =
        std::jthread([this, invitation = std::move(invitation)](std::stop_token stop) mutable {
            try {
                impl->join(std::move(invitation), true, stop);
            } catch (const std::exception& error) {
                impl->failure(error, stop);
            } catch (...) {
                impl->publish(PairingState::failed, "Pairing failed");
            }
        });
}
void PairingSession::browse() {
    impl->browser.request_stop();
    if (impl->browser.joinable())
        impl->browser.join();
#ifdef BEAM_HAS_DISCOVERY
    impl->browser = std::jthread([this](std::stop_token stop) {
        try {
            DiscoveryOptions options;
            options.pairing = true;
            options.stop_token = stop;
            const auto devices = discover_devices(options);
            std::vector<PairingDevice> nearby;
            const auto local = snapshot().port;
            const auto addresses =
                impl->options.addresses.empty() ? net::local_addresses() : impl->options.addresses;
            for (const auto& device : devices)
                for (const auto& endpoint : device.endpoints)
                    if (!(endpoint.port == local && std::find(addresses.begin(), addresses.end(),
                                                              endpoint.host) != addresses.end()) &&
                        nearby.size() < 128)
                        nearby.push_back({device.name, endpoint_string(endpoint)});
            std::lock_guard lock(impl->mutex);
            impl->value.nearby = std::move(nearby);
        } catch (const std::exception&) { /* QR and invitation remain available. */
        }
    });
#endif
}
void PairingSession::approve(const std::string& alias) {
    require(is_peer_name(alias),
            "Alias must use lowercase letters, digits, '-' or '_', starting with a letter");
    std::lock_guard lock(impl->mutex);
    require(impl->value.state == PairingState::approval && impl->approval_alias.empty(),
            "No pairing awaits approval");
    impl->approval_alias = alias;
    impl->value.state = PairingState::waiting;
    impl->value.message = "Approved locally. Waiting for the other device.";
    impl->changed.notify_all();
}
void PairingSession::cancel() {
    impl->stop();
    const bool pending = IdentityStore(impl->options.state_directory).has_incomplete_pairing();
    impl->publish(pending ? PairingState::incomplete : PairingState::cancelled,
                  pending ? "Pairing incomplete. Retry with the same device and alias, or forget "
                            "the pending approval."
                          : "Pairing cancelled.");
}
void PairingSession::reject() {
    std::lock_guard lock(impl->mutex);
    require(impl->value.state == PairingState::approval, "No pairing awaits approval");
    impl->rejected = true;
    impl->changed.notify_all();
}
void PairingSession::forget_incomplete() {
    impl->stop();
    IdentityStore(impl->options.state_directory).forget_incomplete_pairing();
    impl->publish(
        PairingState::idle,
        "Pending approval forgotten. Any already paired device remains in the device list.");
}
PairingSnapshot PairingSession::snapshot() const {
    std::lock_guard lock(impl->mutex);
    return impl->value;
}
} // namespace beam
