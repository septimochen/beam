#pragma once

#include "beam/identity.hpp"
#include "beam/transfer.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace beam::desktop {
struct Config {
    std::filesystem::path state_directory;
    std::uint16_t port{4269};
    bool advertise{true};
    std::chrono::milliseconds timeout{30000};
};
struct SendRequest {
    std::string peer;
    ContentKind kind{ContentKind::file};
    std::filesystem::path file;
    std::string content;
};
enum class Direction { send, receive, check };
enum class EventKind { listening, progress, completed, failed, warning, stopped };
struct Event {
    Direction direction;
    EventKind kind;
    std::string message{};
    TransferResult result{};
    TransferProgress progress{};
};
// GUI-independent worker owner. Control methods run on one application thread;
// only the bounded event mailbox and busy flags cross the worker boundary.
class Service {
    Config config;
    std::mutex mutex;
    std::deque<Event> events;
    std::optional<Event> send_progress, receive_progress;
    std::atomic<bool> sending{false}, receiving{false}, checking{false};
    std::jthread sender, receiver, checker;
    void stop_workers();
    void publish(Event event);
    void receive_loop(std::stop_token stop, const std::filesystem::path& directory);

  public:
    explicit Service(Config configuration);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    void send(SendRequest request);
    void start_receiving(const std::filesystem::path& directory);
    void check_peer(std::string peer);
    void cancel_check();
    void cancel_send();
    void stop_receiving();
    // Joins workers before removing trust, so no old credential snapshot survives.
    // The frontend may restart reception for the remaining peers afterwards.
    void unpair(const std::string& peer);
    TrustedPeer pair_peer(const std::string& name, const std::filesystem::path& certificate,
                          const std::string& fingerprint, const std::string& endpoint);
    TrustedPeer edit_peer(const std::string& peer, const PeerEdit& edit);
    DeviceBackup import_devices(const std::string& contents);
    [[nodiscard]] bool is_sending() const noexcept;
    [[nodiscard]] bool is_receiving() const noexcept;
    [[nodiscard]] bool is_checking() const noexcept;
    [[nodiscard]] std::vector<Event> take_events();
};
// Validate selection before any worker or network operation. Drops are local,
// single regular files; text snapshots use the core's existing bounds.
void validate_request(const SendRequest& request);
} // namespace beam::desktop
