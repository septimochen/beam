#include "beam/pairing.hpp"
#include "beam/routing.hpp"
#include "protocol/pairing.hpp"
#include "transport/pairing_tls.hpp"
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

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
        fails([&] { joiner.join_pin("127.0.0.1:1", "12345"); });
        // Prove PIN authentication failure cannot add either peer.
        joiner.join_pin(beam::endpoint_string(decoded.endpoints[0]),
                        invitation.pin == "000000" ? "000001" : "000000");
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (joiner.snapshot().state != beam::PairingState::failed &&
               std::chrono::steady_clock::now() < limit)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(joiner.snapshot().state == beam::PairingState::failed, "wrong PIN must fail");
        require(beam::IdentityStore(root / "host").peers().empty() &&
                    beam::IdentityStore(root / "phone").peers().empty(),
                "wrong PIN granted trust");
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
        // Publication survives process restart and never overwrites an alias.
        auto conflicting = host_peer;
        conflicting.name = "different-alias";
        fails([&] { pending.prepare_pairing(conflicting); });
        pending.prepare_pairing(host_peer);
        pending.forget_incomplete_pairing();
        require(pending.peers().size() == 1, "forget removed a previously committed peer");
        // PIN success uses the same full-certificate proof and reciprocal flow.
        host.host();
        invitation = await(host, beam::PairingState::hosting);
        joiner.join_pin(beam::endpoint_string(
                            beam::pairing_protocol::decode(invitation.invitation).endpoints[0]),
                        invitation.pin);
        host.approve(await(host, beam::PairingState::approval).alias);
        joiner.approve(await(joiner, beam::PairingState::approval).alias);
        await(host, beam::PairingState::paired);
        await(joiner, beam::PairingState::paired);
        host.host();
        await(host, beam::PairingState::hosting);
        host.cancel();
        require(host.snapshot().invitation.empty() && host.snapshot().pin.empty(),
                "cancel retained invitation");
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
        // Pair over scoped IPv6-capable sockets as well as IPv4.
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
        const auto limited_endpoint = beam::endpoint_string(
            beam::pairing_protocol::decode(invitation.invitation).endpoints[0]);
        for (int attempt = 0; attempt < 5; ++attempt) {
            other.join_pin(limited_endpoint, invitation.pin == "000000" ? "000001" : "000000");
            await(other, beam::PairingState::failed);
        }
        await(limited, beam::PairingState::failed);
        require(limited.snapshot().invitation.empty(), "attempt limit retained invitation");
        auto expired_options = options("expired");
        expired_options.lifetime = std::chrono::milliseconds(100);
        beam::PairingSession expired(expired_options);
        expired.host();
        await(expired, beam::PairingState::expired);
        require(expired.snapshot().pin.empty(), "expired PIN retained");
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
                     "journal, PIN, and cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
}
