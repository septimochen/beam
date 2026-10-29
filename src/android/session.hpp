#pragma once

#include "desktop/service.hpp"
#ifdef BEAM_HAS_PAIRING
#include "beam/pairing.hpp"
#endif

namespace beam::android {
// Value-only boundary shared by JNI and host integration tests. Called on one
// application thread; Service owns and joins all network workers.
enum class Command {
    identity,
    peers,
    pair,
    unpair,
    endpoint,
    send,
    receive,
    cancel,
    stop,
    events,
    busy,
    check,
    pairing_host,
    pairing_join,
    // 14 was PIN pairing; keep remaining command IDs stable.
    pairing_approve = 15,
    pairing_snapshot,
    // 17 was nearby pairing discovery.
    pairing_cancel = 18,
    pairing_forget,
    pairing_reject,
    device_details,
    edit_device,
    export_devices,
    preview_devices,
    import_devices
};
class Session {
    std::filesystem::path state;
    desktop::Service service;
#ifdef BEAM_HAS_PAIRING
    std::unique_ptr<PairingSession> pairing;
    std::uint16_t transfer_port;
#endif

  public:
    explicit Session(const std::filesystem::path& directory, std::uint16_t port = 4269,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
    std::vector<std::string> call(Command command, const std::vector<std::string>& arguments);
};
} // namespace beam::android
