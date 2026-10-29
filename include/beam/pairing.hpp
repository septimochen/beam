#pragma once
#include "beam/identity.hpp"
#include <chrono>
#include <memory>

namespace beam {
enum class PairingState {
    idle,
    hosting,
    connecting,
    verifying,
    approval,
    saving,
    paired,
    expired,
    rejected,
    incomplete,
    failed,
    cancelled,
    waiting
};
struct PairingSnapshot {
    PairingState state{PairingState::idle};
    std::string message, invitation, remote_name, alias;
    std::uint16_t port{};
    // A square QR matrix represented as rows of '0' and '1'.
    std::vector<std::string> qr;
};
struct PairingOptions {
    std::filesystem::path state_directory;
    std::string device_name{"Beam device"};
    std::uint16_t transfer_port{4269};
    std::uint16_t listen_port{};
    std::chrono::milliseconds lifetime{std::chrono::minutes(3)};
    // Empty selects multicast-capable local interfaces. Explicit numeric local
    // addresses are useful for hosts with multiple links and localhost tests.
    std::vector<std::string> addresses{};
};
// Application-thread control, value snapshots, joined stop-token workers.
// No callback enters a UI, JNI, or a TLS implementation callback.
class PairingSession {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    explicit PairingSession(PairingOptions options);
    ~PairingSession();
    PairingSession(const PairingSession&) = delete;
    PairingSession& operator=(const PairingSession&) = delete;
    void host();
    void join(const std::string& invitation);
    void approve(const std::string& alias);
    void reject();
    void cancel();
    void forget_incomplete();
    [[nodiscard]] PairingSnapshot snapshot() const;
};
[[nodiscard]] const char* pairing_state_name(PairingState state) noexcept;
[[nodiscard]] std::vector<std::string> pairing_qr(const std::string& invitation);
} // namespace beam
