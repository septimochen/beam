#include "beam/pairing.hpp"
#include "protocol/pairing.hpp"
#include "transport/pairing_tls.hpp"
#include <atomic>
#include <chrono>
#ifdef __APPLE__
#include <unistd.h>
#else
#include <stdlib.h>
#endif
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/pem.h>
#include <thread>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class Action> void fails(Action action) {
    bool caught = false;
    try {
        action();
    } catch (const std::exception&) {
        caught = true;
    }
    require(caught, "expected rejection");
}
beam::PairingSnapshot await(beam::PairingSession& session, beam::PairingState state) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = session.snapshot();
        if (snapshot.state == state)
            return snapshot;
        if (snapshot.state == beam::PairingState::failed ||
            snapshot.state == beam::PairingState::expired ||
            snapshot.state == beam::PairingState::incomplete)
            throw std::runtime_error(std::string("Await ") + beam::pairing_state_name(state) +
                                     " saw " + beam::pairing_state_name(snapshot.state) + ": " +
                                     snapshot.message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Pairing timed out: " + session.snapshot().message);
}
// Exercise the TLS verifier directly, bypassing the earlier identity-frame checks.
void reject_proof(const beam::Credentials& server_credentials, const beam::DeviceIdentity& client,
                  const beam::Credentials& client_credentials, const beam::DeviceIdentity& server) {
    using namespace beam::pairing_transport;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    Listener listener(0);
    auto outgoing = connect({"127.0.0.1", listener.port}, {}, deadline);
    auto incoming = listener.accept({}, deadline);
    std::string server_error, client_error;
    std::jthread worker([&] {
        try {
            Tls proof(std::move(incoming), true, {}, server_credentials, client, {}, deadline);
            proof.send({"proof"});
            static_cast<void>(proof.read());
        } catch (const std::exception& error) {
            server_error = error.what();
        }
    });
    try {
        Tls proof(std::move(outgoing), false, {}, client_credentials, server, {}, deadline);
        static_cast<void>(proof.read());
        proof.send({"proof"});
    } catch (const std::exception& error) {
        client_error = error.what();
    }
    worker.join();
    require((!server_error.empty() || !client_error.empty()) &&
                server_error.find("Pairing expired") == std::string::npos &&
                client_error.find("Pairing expired") == std::string::npos,
            "Invalid certificate proof accepted or timed out");
}
beam::DeviceIdentity dated_certificate(const beam::DeviceIdentity& identity,
                                       const beam::Credentials& credentials,
                                       const std::filesystem::path& output, bool future) {
    std::unique_ptr<BIO, decltype(&BIO_free)> input(
        BIO_new_mem_buf(identity.certificate_pem.data(),
                        static_cast<int>(identity.certificate_pem.size())),
        BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(
        PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> key_input(
        BIO_new_file(credentials.private_key.c_str(), "r"), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        PEM_read_bio_PrivateKey(key_input.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    require(certificate && key &&
                X509_gmtime_adj(X509_getm_notBefore(certificate.get()), future ? 3600 : -3600) &&
                X509_gmtime_adj(X509_getm_notAfter(certificate.get()), future ? 7200 : -60) &&
                X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0,
            "Create dated certificate fixture failed");
    std::unique_ptr<BIO, decltype(&BIO_free)> encoded(BIO_new(BIO_s_mem()), BIO_free);
    require(encoded && PEM_write_bio_X509(encoded.get(), certificate.get()) == 1,
            "Encode dated certificate failed");
    char* data = nullptr;
    const auto size = BIO_get_mem_data(encoded.get(), &data);
    auto result = identity;
    result.certificate_pem.assign(data, static_cast<std::size_t>(size));
    std::array<unsigned char, 32> digest{};
    unsigned count = 0;
    require(X509_digest(certificate.get(), EVP_sha256(), digest.data(), &count) == 1 && count == 32,
            "Fingerprint dated certificate failed");
    result.fingerprint.clear();
    for (const auto byte : digest) {
        result.fingerprint += "0123456789abcdef"[byte >> 4];
        result.fingerprint += "0123456789abcdef"[byte & 15];
    }
    std::ofstream(output) << result.certificate_pem;
    return result;
}
} // namespace
int main() {
    std::array<char, 64> pattern{};
    const std::string prefix = "/private/tmp/beam-seamless-XXXXXX";
#ifdef __linux__
    const std::string location = "/tmp/beam-seamless-XXXXXX";
#else
    const auto& location = prefix;
#endif
    std::copy(location.begin(), location.end(), pattern.begin());
    const auto* created = mkdtemp(pattern.data());
    if (!created)
        return 1;
    const std::filesystem::path root(created);
    try {
        const auto options = [&](const char* name) {
            return beam::PairingOptions{root / name,  name, 4269, 0, std::chrono::seconds(30),
                                        {"127.0.0.1"}};
        };
        beam::PairingSession host(options("host")), joiner(options("phone"));
        host.host();
        auto invitation = await(host, beam::PairingState::hosting);
        const auto decoded = beam::pairing_protocol::decode(invitation.invitation);
        require(beam::pairing_protocol::encode(decoded) == invitation.invitation &&
                    !invitation.qr.empty(),
                "invitation round trip");
        fails([&] { joiner.join(invitation.invitation + "!"); });
        fails([&] { static_cast<void>(beam::pairing_protocol::decode(std::string(2049, 'a'))); });
        fails([&] { static_cast<void>(beam::pairing_protocol::unpack(std::string(4, '\xff'))); });
        fails([&] {
            static_cast<void>(beam::pairing_protocol::unpack(std::string("\0\0\0\1\0", 5)));
        });
        auto old_invitation = invitation.invitation;
        old_invitation.replace(0, 12, "beam-pair:1:");
        fails([&] { joiner.join(old_invitation); });
        const auto legacy_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        auto legacy_socket =
            beam::pairing_transport::connect(decoded.endpoints[0], {}, legacy_deadline);
        beam::pairing_transport::send_hello(
            legacy_socket, {"beam-pair/1", "invitation", decoded.id}, {}, legacy_deadline);
        fails([&] {
            beam::pairing_transport::Tls legacy(std::move(legacy_socket), false, decoded.secret, {},
                                                {}, {}, legacy_deadline);
        });
        fails([&] {
            beam::pairing_transport::Tls short_secret({}, false, "123456", {}, {}, {},
                                                      legacy_deadline);
        });
        // A wrong 256-bit invitation secret cannot add either peer.
        auto wrong = decoded;
        wrong.secret[0] = wrong.secret[0] == '0' ? '1' : '0';
        joiner.join(beam::pairing_protocol::encode(wrong));
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (joiner.snapshot().state != beam::PairingState::failed &&
               std::chrono::steady_clock::now() < limit)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(joiner.snapshot().state == beam::PairingState::failed, "wrong secret must fail");
        require(beam::IdentityStore(root / "host").peers().empty() &&
                    beam::IdentityStore(root / "phone").peers().empty(),
                "wrong secret granted trust");
        joiner.join(invitation.invitation);
        const auto a = await(host, beam::PairingState::approval);
        const auto b = await(joiner, beam::PairingState::approval);
        require(beam::IdentityStore(root / "host").peers().empty(),
                "proof before approval granted trust");
        host.approve(a.alias);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(beam::IdentityStore(root / "host").peers().empty(), "one approval granted trust");
        joiner.approve(b.alias);
        await(host, beam::PairingState::paired);
        await(joiner, beam::PairingState::paired);
        const auto host_peer = beam::IdentityStore(root / "host").peer("phone");
        require(host_peer.identity.fingerprint ==
                    beam::IdentityStore(root / "phone").identity().fingerprint,
                "host saved incorrect certificate");
        require(!beam::IdentityStore(root / "host").has_incomplete_pairing(),
                "completed journal retained");
        require(!beam::IdentityStore(root / "phone").has_incomplete_pairing(),
                "completed journal retained");
        // Replay cannot reconnect once the single-use window has closed.
        joiner.join(invitation.invitation);
        const auto replay_limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (joiner.snapshot().state != beam::PairingState::failed &&
               std::chrono::steady_clock::now() < replay_limit)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(joiner.snapshot().state == beam::PairingState::failed,
                "invitation replay accepted");
        // A pending record does not participate in the transfer trust snapshot.
        beam::IdentityStore pending(root / "pending", true);
        pending.initialize();
        pending.prepare_pairing(host_peer);
        require(pending.peers().empty(), "pending approval became a peer");
        fails([&] { static_cast<void>(pending.credentials()); });
        static_cast<void>(pending.commit_pairing());
        static_cast<void>(pending.commit_pairing());
        require(pending.peers().size() == 1, "idempotent commit duplicated peer");
        pending.finish_pairing();
        // Reopen after each durable boundary as if the process stopped before
        // the next acknowledgement. Pending approval alone cannot authorize TLS.
        {
            beam::IdentityStore recovery(root / "recovery", true);
            recovery.initialize();
            recovery.prepare_pairing(host_peer);
        }
        {
            beam::IdentityStore recovery(root / "recovery");
            require(recovery.has_incomplete_pairing() && recovery.peers().empty(),
                    "restart promoted pending approval");
            fails([&] { static_cast<void>(recovery.credentials()); });
            static_cast<void>(recovery.commit_pairing());
        }
        {
            beam::IdentityStore recovery(root / "recovery");
            require(recovery.has_incomplete_pairing(), "restart lost incomplete receipt state");
            static_cast<void>(recovery.commit_pairing());
            require(recovery.peers().size() == 1 &&
                        recovery.peer(host_peer.name).identity.fingerprint ==
                            host_peer.identity.fingerprint,
                    "restart duplicated or replaced pinned trust");
            recovery.finish_pairing();
        }
        // Publication survives process restart and never overwrites an alias.
        {
            beam::IdentityStore lost(root / "lost-key", true);
            lost.initialize();
            lost.prepare_pairing(host_peer);
            std::filesystem::remove(root / "lost-key" / "identity.pem");
            fails([&] { static_cast<void>(lost.initialize()); });
        }
        auto conflicting = host_peer;
        conflicting.name = "different-alias";
        fails([&] { pending.prepare_pairing(conflicting); });
        pending.prepare_pairing(host_peer);
        pending.forget_incomplete_pairing();
        require(pending.peers().size() == 1, "forget removed a previously committed peer");
        pending.prepare_pairing(host_peer);
        pending.unpair(host_peer.name);
        require(pending.peers().empty() && !pending.has_incomplete_pairing(),
                "unpair retained trust or its interrupted pairing journal");
        fails([&] { static_cast<void>(pending.commit_pairing()); });
        const auto other_peer = beam::IdentityStore(root / "phone").peer("host");
        pending.prepare_pairing(host_peer);
        static_cast<void>(pending.commit_pairing());
        pending.finish_pairing();
        pending.prepare_pairing(other_peer);
        pending.unpair(host_peer.name);
        require(pending.peers().empty() && pending.has_incomplete_pairing(),
                "unpair discarded another device's pending approval");
        pending.forget_incomplete_pairing();
        host.host();
        await(host, beam::PairingState::hosting);
        host.cancel();
        require(host.snapshot().invitation.empty(), "cancel retained invitation");
        // A substituted QR fingerprint is rejected even with the right secret.
        beam::PairingSession other(options("other"));
        host.host();
        invitation = await(host, beam::PairingState::hosting);
        auto substituted = beam::pairing_protocol::decode(invitation.invitation);
        substituted.fingerprint = std::string(64, '0');
        other.join(beam::pairing_protocol::encode(substituted));
        await(other, beam::PairingState::failed);
        host.cancel();
        require(beam::IdentityStore(root / "other").peers().empty(),
                "substituted certificate granted trust");
        // Either side can decline before the other has approved.
        host.host();
        invitation = await(host, beam::PairingState::hosting);
        other.join(invitation.invitation);
        await(host, beam::PairingState::approval);
        await(other, beam::PairingState::approval);
        host.reject();
        await(host, beam::PairingState::rejected);
        await(other, beam::PairingState::rejected);
        require(beam::IdentityStore(root / "other").peers().empty(),
                "declined pairing granted trust");
        // Fresh TLS key proof must reject a certificate with another key.
        const auto phone_identity = beam::IdentityStore(root / "phone").identity();
        const auto host_identity = beam::IdentityStore(root / "host").identity();
        auto bad_credentials =
            beam::IdentityStore(root / "phone").pairing_credentials(host_identity);
        bad_credentials.private_key = root / "host" / "identity.pem";
        const auto server_credentials =
            beam::IdentityStore(root / "host").pairing_credentials(phone_identity);
        const auto key_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        beam::pairing_transport::Listener key_listener(0);
        auto client_socket =
            beam::pairing_transport::connect({"127.0.0.1", key_listener.port}, {}, key_deadline);
        auto server_socket = key_listener.accept({}, key_deadline);
        std::string server_error, client_error;
        std::jthread key_server([&] {
            try {
                beam::pairing_transport::Tls proof(std::move(server_socket), true, {},
                                                   server_credentials, phone_identity, {},
                                                   key_deadline);
            } catch (const std::exception& error) {
                server_error = error.what();
            }
        });
        try {
            beam::pairing_transport::Tls invalid(std::move(client_socket), false, {},
                                                 bad_credentials, host_identity, {}, key_deadline);
            invalid.send({"key-proof"});
            static_cast<void>(invalid.read());
        } catch (const std::exception& error) {
            client_error = error.what();
        }
        key_server.join();
        require(!server_error.empty() && !client_error.empty() &&
                    server_error.find("expired") == std::string::npos &&
                    client_error.find("expired") == std::string::npos,
                "mismatched private key: server=" + server_error + "; client=" + client_error);
        const auto client_credentials =
            beam::IdentityStore(root / "phone").pairing_credentials(host_identity);
        auto wrong_host = host_identity;
        wrong_host.tls_name = "beam-00000000000000000000000000000000";
        reject_proof(server_credentials, phone_identity, client_credentials, wrong_host);
        auto wrong_pin = host_identity;
        wrong_pin.fingerprint = std::string(64, '0');
        reject_proof(server_credentials, phone_identity, client_credentials, wrong_pin);
        auto wrong_client = phone_identity;
        wrong_client.fingerprint = std::string(64, '0');
        reject_proof(server_credentials, wrong_client, client_credentials, host_identity);
        for (const bool future : {false, true}) {
            auto dated_credentials = server_credentials;
            dated_credentials.certificate = root / (future ? "future.pem" : "expired.pem");
            const auto dated = dated_certificate(host_identity, server_credentials,
                                                 dated_credentials.certificate, future);
            reject_proof(dated_credentials, phone_identity, client_credentials, dated);
        }
        // Pair over IPv6-capable sockets as well as IPv4.
        auto v6_options = options("ipv6");
        v6_options.addresses = {"::1"};
        beam::PairingSession ipv6(v6_options);
        ipv6.host();
        invitation = await(ipv6, beam::PairingState::hosting);
        other.join(invitation.invitation);
        ipv6.approve(await(ipv6, beam::PairingState::approval).alias);
        other.approve(await(other, beam::PairingState::approval).alias);
        await(ipv6, beam::PairingState::paired);
        await(other, beam::PairingState::paired);
        // All failed authentication attempts consume the same finite window.
        auto limited_options = options("limited");
        limited_options.lifetime = std::chrono::seconds(20);
        beam::PairingSession limited(limited_options);
        limited.host();
        invitation = await(limited, beam::PairingState::hosting);
        auto limited_wrong = beam::pairing_protocol::decode(invitation.invitation);
        limited_wrong.secret[0] = limited_wrong.secret[0] == '0' ? '1' : '0';
        for (int attempt = 0; attempt < 5; ++attempt) {
            other.join(beam::pairing_protocol::encode(limited_wrong));
            await(other, beam::PairingState::failed);
        }
        await(limited, beam::PairingState::failed);
        require(limited.snapshot().invitation.empty(), "attempt limit retained invitation");
        auto expired_options = options("expired");
        expired_options.lifetime = std::chrono::milliseconds(100);
        beam::PairingSession expired(expired_options);
        expired.host();
        await(expired, beam::PairingState::expired);
        require(expired.snapshot().invitation.empty(), "expired invitation retained");
        // Newly paired records authenticate real QUIC content, in both directions.
        for (const bool reverse : {false, true}) {
            beam::pairing_transport::Listener reservation(0);
            const auto port = reservation.port;
            const auto sender_state = root / (reverse ? "phone" : "host");
            const auto receiver_state = root / (reverse ? "host" : "phone");
            const auto receiver_identity = beam::IdentityStore(receiver_state).identity();
            beam::TransferOptions receive_options;
            receive_options.credentials = beam::IdentityStore(receiver_state).credentials();
            receive_options.timeout = std::chrono::seconds(5);
            std::atomic<bool> listening{false};
            std::exception_ptr failure;
            receive_options.on_listening = [&] { listening = true; };
            beam::TransferResult received;
            const auto output = root / (reverse ? "text-output" : "file-output");
            std::filesystem::create_directory(output);
            std::jthread receiver([&] {
                try {
                    received = beam::receive_content(port, output, receive_options);
                } catch (...) {
                    failure = std::current_exception();
                    listening = true;
                }
            });
            while (!listening)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            beam::TransferOptions send_options;
            send_options.credentials = beam::IdentityStore(sender_state).credentials();
            send_options.timeout = std::chrono::seconds(5);
            if (reverse)
                beam::send_text({"127.0.0.1", port}, "paired text", receiver_identity.tls_name,
                                send_options);
            else {
                const auto source = root / "paired-file.txt";
                {
                    std::ofstream file(source);
                    file << "paired file";
                }
                beam::send_file({"127.0.0.1", port}, source, receiver_identity.tls_name,
                                send_options);
            }
            receiver.join();
            if (failure)
                std::rethrow_exception(failure);
            require(reverse ? received.text == "paired text" : received.bytes == 11,
                    "paired QUIC content mismatch");
        }
        std::cout << "Seamless pairing authentication, key proof, reciprocal approval, replay, "
                     "journal, version rejection, and cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
}
