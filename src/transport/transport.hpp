#pragma once
#include "beam/transfer.hpp"
#include <memory>
#include <span>

namespace beam::transport {
// A connection must outlive its streams; operations on each stream are serialized.
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
};
std::unique_ptr<Connection> connect(const Endpoint& endpoint, const std::string& server_name,
                                    const TransferOptions& options);
std::unique_ptr<Connection> listen(std::uint16_t port, const TransferOptions& options);
void read_exact(Stream& stream, std::span<std::byte> bytes);
} // namespace beam::transport
