#pragma once

#include "beam/transfer.hpp"
#include <memory>
#include <string_view>
#include <vector>

namespace beam {
struct DeviceIdentity {
    std::string fingerprint;
    std::string tls_name;
    std::string certificate_pem;
};
struct TrustedPeer {
    std::string name;
    std::string endpoint;
    DeviceIdentity identity;
};
[[nodiscard]] std::filesystem::path default_state_directory();
[[nodiscard]] bool is_peer_name(std::string_view name) noexcept;
// The private store is locked for this object's lifetime. Pairing requires the
// full SHA-256 certificate fingerprint verified through an independent channel.
class IdentityStore {
    struct Impl;
    std::unique_ptr<Impl> implementation;

  public:
    explicit IdentityStore(const std::filesystem::path& directory, bool create = false);
    ~IdentityStore();
    IdentityStore(const IdentityStore&) = delete;
    IdentityStore& operator=(const IdentityStore&) = delete;
    DeviceIdentity initialize();
    [[nodiscard]] DeviceIdentity identity() const;
    TrustedPeer pair(const std::string& name, const std::filesystem::path& certificate,
                     const std::string& verified_fingerprint, const std::string& endpoint = {});
    [[nodiscard]] std::vector<TrustedPeer> peers() const;
    [[nodiscard]] TrustedPeer peer(const std::string& name) const;
    void unpair(const std::string& name);
    void set_endpoint(const std::string& name, const std::string& endpoint);
    // Empty name authorizes all currently paired peers (receiver); a sender
    // authorizes exactly one peer. An empty trust set always fails closed.
    [[nodiscard]] Credentials credentials(const std::string& name = {}) const;
};
} // namespace beam
