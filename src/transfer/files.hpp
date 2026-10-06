#pragma once
#include "protocol/codec.hpp"
#include <filesystem>
#include <memory>
#include <span>

namespace beam::transfer {
class Hash {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Hash();
    ~Hash();
    void update(std::span<const std::byte> bytes);
    protocol::Digest finish();
};
class Source {
    int descriptor{-1};

  public:
    std::uint64_t size{};
    explicit Source(const std::filesystem::path& path);
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    std::size_t read(std::span<std::byte> bytes);
    void rewind();
};
class ReceiveDirectory {
    friend class Destination;
    int descriptor{-1};

  public:
    explicit ReceiveDirectory(const std::filesystem::path& path);
    ~ReceiveDirectory();
    ReceiveDirectory(const ReceiveDirectory&) = delete;
    ReceiveDirectory& operator=(const ReceiveDirectory&) = delete;
};
class Destination {
    int directory{-1}, descriptor{-1};
    std::string temporary, filename;

  public:
    Destination(const std::filesystem::path& output, const std::string& name);
    Destination(const ReceiveDirectory& output, const std::string& name);
    ~Destination();
    Destination(const Destination&) = delete;
    Destination& operator=(const Destination&) = delete;
    void write(std::span<const std::byte> bytes);
    void commit();
};
std::uint64_t random_id();
} // namespace beam::transfer
