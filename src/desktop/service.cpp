#include "service.hpp"
#include "beam/beam.hpp"
#include "beam/discovery.hpp"
#include "beam/identity.hpp"
#include "beam/routing.hpp"
#include <algorithm>
#include <condition_variable>

namespace beam::desktop {
void validate_request(const SendRequest& request) {
    if (!is_peer_name(request.peer))
        throw std::invalid_argument("Choose a paired device before sending");
    if (request.kind == ContentKind::file) {
        if (request.file.string().find('\0') != std::string::npos ||
            !is_safe_filename(request.file.filename().string()) ||
            !std::filesystem::is_regular_file(std::filesystem::symlink_status(request.file)))
            throw std::invalid_argument("Choose one regular file with a supported filename");
    } else {
        validate_content(request.kind, request.content);
    }
}
Service::Service(Config configuration) : config(std::move(configuration)) {
    if (!config.port || config.timeout.count() <= 0 || config.timeout > std::chrono::hours(24))
        throw std::invalid_argument("Invalid desktop listen port or timeout");
}
Service::~Service() { stop_workers(); }
void Service::stop_workers() {
    cancel_send();
    stop_receiving();
    cancel_check();
    // Join while the event mailbox and configuration still exist.
    if (sender.joinable())
        sender.join();
    if (receiver.joinable())
        receiver.join();
    if (checker.joinable())
        checker.join();
}
bool Service::is_sending() const noexcept { return sending.load(); }
bool Service::is_receiving() const noexcept { return receiving.load(); }
bool Service::is_checking() const noexcept { return checking.load(); }
void Service::cancel_check() { checker.request_stop(); }
void Service::cancel_send() { sender.request_stop(); }
void Service::stop_receiving() { receiver.request_stop(); }
void Service::unpair(const std::string& name) {
    {
        IdentityStore store(config.state_directory);
        static_cast<void>(store.peer(name));
    }
    // Release the store lock before joining workers that may be loading trust.
    stop_workers();
    IdentityStore(config.state_directory).unpair(name);
}
TrustedPeer Service::pair_peer(const std::string& name, const std::filesystem::path& certificate,
                               const std::string& fingerprint, const std::string& endpoint) {
    stop_workers();
    return IdentityStore(config.state_directory).pair(name, certificate, fingerprint, endpoint);
}
TrustedPeer Service::edit_peer(const std::string& name, const PeerEdit& edit) {
    stop_workers();
    return IdentityStore(config.state_directory).edit_peer(name, edit);
}
DeviceBackup Service::import_devices(const std::string& contents) {
    {
        IdentityStore store(config.state_directory);
        static_cast<void>(store.preview_import(contents));
    }
    stop_workers();
    return IdentityStore(config.state_directory).import_devices(contents);
}
void Service::publish(Event event) {
    std::lock_guard lock(mutex);
    auto& latest = event.direction == Direction::send ? send_progress : receive_progress;
    if (event.kind == EventKind::progress) {
        latest = std::move(event);
        return;
    }
    if (event.direction != Direction::check)
        latest.reset();
    // The UI drains this every 100 ms. Bound delayed/hidden UI consumption too.
    if (events.size() == 64)
        events.pop_front();
    events.push_back(std::move(event));
}
std::vector<Event> Service::take_events() {
    std::lock_guard lock(mutex);
    std::vector<Event> result;
    result.reserve(events.size() + 2);
    result.insert(result.end(), std::make_move_iterator(events.begin()),
                  std::make_move_iterator(events.end()));
    events.clear();
    for (auto* latest : {&send_progress, &receive_progress}) {
        if (*latest) {
            result.push_back(std::move(**latest));
            latest->reset();
        }
    }
    return result;
}
void Service::send(SendRequest request) {
    validate_request(request);
    if (sending.exchange(true))
        throw std::logic_error("A send is already running");
    try {
        if (sender.joinable())
            sender.join();
        sender = std::jthread([this, request = std::move(request)](std::stop_token stop) {
            try {
                TransferOptions options;
                TrustedPeer peer;
                {
                    IdentityStore store(config.state_directory);
                    peer = store.peer(request.peer);
                    options.credentials = store.credentials(request.peer);
                }
                const auto endpoints = peer_endpoints(peer);
                if (endpoints.empty())
                    throw std::invalid_argument(
                        "Peer has no saved endpoint; update it with beam peer");
                options.fallback_endpoints.assign(endpoints.begin() + 1, endpoints.end());
                options.timeout = config.timeout;
                if (endpoints.size() > 1)
                    options.connect_timeout =
                        std::min(config.timeout, std::chrono::milliseconds(3000));
                options.stop_token = stop;
                options.on_progress = [this](const TransferProgress& progress) {
                    publish({Direction::send, EventKind::progress, {}, {}, progress});
                };
                const auto result = request.kind == ContentKind::file
                                        ? send_file(endpoints.front(), request.file,
                                                    peer.identity.tls_name, options)
                                        : send_text(endpoints.front(), request.content,
                                                    peer.identity.tls_name, options);
                publish({Direction::send, EventKind::completed, request.peer, result});
            } catch (const std::exception& error) {
                publish({Direction::send, EventKind::failed, error.what()});
            }
            sending.store(false);
        });
    } catch (...) {
        sending.store(false);
        throw;
    }
}
void Service::check_peer(std::string name) {
    if (!is_peer_name(name))
        throw std::invalid_argument("Choose a paired device before checking connectivity");
    if (checking.exchange(true))
        throw std::logic_error("A connectivity check is already running");
    try {
        if (checker.joinable())
            checker.join();
        checker = std::jthread([this, name = std::move(name)](std::stop_token stop) {
            try {
                TransferOptions options;
                TrustedPeer peer;
                {
                    IdentityStore store(config.state_directory);
                    peer = store.peer(name);
                    options.credentials = store.credentials(name);
                }
                const auto endpoints = peer_endpoints(peer);
                if (endpoints.empty())
                    throw std::invalid_argument("Peer has no saved endpoint; update its address");
                options.fallback_endpoints.assign(endpoints.begin() + 1, endpoints.end());
                options.timeout = std::min(config.timeout, std::chrono::milliseconds(5000));
                options.connect_timeout =
                    std::min(options.timeout, std::chrono::milliseconds(2000));
                options.stop_token = stop;
                check_connectivity(endpoints.front(), peer.identity.tls_name, options);
                publish({Direction::check, EventKind::completed,
                         name + " is reachable (authenticated)"});
            } catch (const std::exception& error) {
                publish({Direction::check, EventKind::failed,
                         name + " is unavailable: " + error.what()});
            }
            checking.store(false);
        });
    } catch (...) {
        checking.store(false);
        throw;
    }
}
void Service::start_receiving(const std::filesystem::path& directory) {
    if (receiving.exchange(true))
        throw std::logic_error("Receiver is already running");
    try {
        if (receiver.joinable())
            receiver.join();
        receiver = std::jthread([this, directory](std::stop_token stop) {
            receive_loop(stop, directory);
            receiving.store(false);
            publish({Direction::receive, EventKind::stopped});
        });
    } catch (...) {
        receiving.store(false);
        throw;
    }
}
void Service::receive_loop(std::stop_token stop, const std::filesystem::path& directory) {
    while (!stop.stop_requested()) {
        bool listening = false, payload_started = false;
        try {
            TransferOptions options;
            {
                // Reload trust for each listener, and release the store lock before I/O.
                IdentityStore store(config.state_directory);
                options.credentials = store.credentials();
            }
            options.timeout = config.timeout;
            options.stop_token = stop;
#ifdef BEAM_HAS_DISCOVERY
            std::unique_ptr<ReceiverAdvertisement> advertisement;
            options.should_cancel = [&] {
                if (advertisement) {
                    try {
                        advertisement->check();
                    } catch (const DiscoveryError& error) {
                        publish({Direction::receive, EventKind::warning, error.what()});
                        advertisement.reset();
                    }
                }
                return stop.stop_requested();
            };
#endif
            options.on_listening = [&] {
                listening = true;
#ifdef BEAM_HAS_DISCOVERY
                if (config.advertise) {
                    DiscoveryOptions discovery;
                    discovery.stop_token = stop;
                    try {
                        advertisement =
                            std::make_unique<ReceiverAdvertisement>(config.port, "Beam", discovery);
                    } catch (const DiscoveryError& error) {
                        if (error.cancelled)
                            throw;
                        publish({Direction::receive, EventKind::warning, error.what()});
                    }
                }
#endif
                publish({Direction::receive, EventKind::listening});
            };
            options.on_progress = [&](const TransferProgress& progress) {
                // Listener readiness has its own event. Idle waits are not a new
                // transfer and must not overwrite the last verified result.
                if (progress.stage == TransferStage::waiting && progress.filename.empty())
                    return;
                if (progress.stage == TransferStage::transferring)
                    payload_started = true;
                publish({Direction::receive, EventKind::progress, {}, {}, progress});
            };
            const auto result = receive_content(config.port, directory, options);
            if (result.connectivity_check)
                continue;
            if (!result.connectivity_check)
                publish({Direction::receive, EventKind::completed, {}, result});
        } catch (const TransferError& error) {
            if (stop.stop_requested())
                break;
            if (error.code != TransferErrorCode::timeout || payload_started)
                publish({Direction::receive, EventKind::failed, error.what()});
            if (!listening)
                break;
        } catch (const std::exception& error) {
            if (!stop.stop_requested())
                publish({Direction::receive, EventKind::failed, error.what()});
            break;
        }
        // Avoid a busy loop under repeated rejected/failed connections; interruptible.
        std::mutex delay_mutex;
        std::condition_variable_any delay;
        std::unique_lock lock(delay_mutex);
        delay.wait_for(lock, stop, std::chrono::milliseconds(100), [] { return false; });
    }
}
} // namespace beam::desktop
