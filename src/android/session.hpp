#pragma once

#include "desktop/service.hpp"

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
    busy
};
class Session {
    std::filesystem::path state;
    desktop::Service service;

  public:
    explicit Session(const std::filesystem::path& directory, std::uint16_t port = 4269);
    std::vector<std::string> call(Command command, const std::vector<std::string>& arguments);
};
} // namespace beam::android
