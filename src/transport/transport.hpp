#pragma once
#include "beam/transfer.hpp"
#include <algorithm>
#include <condition_variable>
#include <memory>
#include <span>

namespace beam::transport {
// A connection and its TransferOptions must outlive its streams; operations on
// each stream are serialized.
class Stream {
  public:
    virtual ~Stream() = default;
    virtual void send(std::span<const std::byte> bytes) = 0;
    virtual std::size_t read(std::span<std::byte> bytes) = 0;
    virtual void finish() = 0;
};
class Connection {
  public:
    virtual ~Connection() = default;
    virtual std::unique_ptr<Stream> open(bool unidirectional) = 0;
    virtual std::unique_ptr<Stream> accept(bool unidirectional) = 0;
    virtual void abort(TransferErrorCode code) noexcept = 0;
};
inline void check_cancel(const TransferOptions& options) {
    if (options.stop_token.stop_requested() || (options.should_cancel && options.should_cancel()))
        throw TransferError(TransferErrorCode::cancelled, "transfer cancelled locally");
}
// Poll only to observe signal flags/stop tokens; retain the original operation
// deadline across wakeups, including spurious notifications.
template <typename Predicate>
void wait_for_operation(std::condition_variable& changed, std::unique_lock<std::mutex>& lock,
                        const TransferOptions& options, Predicate ready, const char* context) {
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    for (;;) {
        check_cancel(options);
        if (ready())
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            throw TransferError(TransferErrorCode::timeout, context);
        changed.wait_until(lock, std::min(deadline, now + std::chrono::milliseconds(50)));
    }
}
std::unique_ptr<Connection> connect(const Endpoint& endpoint, const std::string& server_name,
                                    const TransferOptions& options);
std::unique_ptr<Connection> listen(std::uint16_t port, const TransferOptions& options);
void read_exact(Stream& stream, std::span<std::byte> bytes);
} // namespace beam::transport
