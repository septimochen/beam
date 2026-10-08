#include "beam/transfer.hpp"
#include "protocol/codec.hpp"
#include "transfer/files.hpp"
#include "transport/transport.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
class Fragmented final : public beam::transport::Stream {
  public:
    std::vector<std::byte> bytes;
    std::size_t position{};
    explicit Fragmented(std::vector<std::byte> data) : bytes(std::move(data)) {}
    void send(std::span<const std::byte>) override { throw std::runtime_error("unexpected send"); }
    void finish() override {}
    std::size_t read(std::span<std::byte> output) override {
        const auto count = std::min({output.size(), bytes.size() - position, std::size_t{1}});
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(position), count, output.begin());
        position += count;
        return count;
    }
};
int attack(int argc, char** argv) {
    if (argc != 7)
        return 2;
    const std::string mode = argv[1];
    const beam::TransferOptions options{{argv[3], argv[4], argv[5]}, std::chrono::seconds(5)};
    auto connection = beam::transport::connect(beam::parse_endpoint(argv[2]), "localhost", options);
    auto control = connection->open(mode == "wrong-direction");
    if (mode == "wrong-direction") {
        const std::array byte{std::byte{1}};
        try {
            control->send(byte);
        } catch (const std::runtime_error&) {
        }
        return 0;
    }
    beam::transfer::Hash hash;
    const std::array data{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    hash.update(data);
    beam::protocol::Message offer{beam::protocol::Type::offer, 42, 3, hash.finish(), "attack.bin"};
    if (mode == "checksum")
        offer.digest = {};
    if (mode == "truncated")
        offer.size = 4;
    if (mode == "oversized")
        offer.size = 2;
    if (mode == "invalid-name")
        offer.text = "badfile";
    if (mode == "wrong-state")
        offer.type = beam::protocol::Type::accept, offer.text.clear();
    auto frame = beam::protocol::encode(offer);
    if (mode == "invalid-name") {
        const std::string bad = "../evil";
        for (std::size_t i = 0; i < bad.size(); ++i)
            frame[frame.size() - bad.size() + i] = static_cast<std::byte>(bad[i]);
    }
    if (mode == "invalid-version")
        frame[0] = std::byte{99};
    try {
        control->send(frame);
        const auto response = beam::protocol::read(*control);
        if (response.type != beam::protocol::Type::accept)
            return 0;
        auto payload = connection->open(true);
        payload->send(beam::protocol::encode_id(mode == "wrong-id" ? 43 : 42));
        if (mode == "interrupted") {
            payload->send(std::span{data}.first(1));
            return 0;
        }
        payload->send(data);
        payload->finish();
        const auto result = beam::protocol::read(*control);
        if (result.type == beam::protocol::Type::complete)
            throw std::logic_error("malicious transfer was accepted");
        if (result.type != beam::protocol::Type::failed)
            throw std::logic_error("unexpected malicious transfer result");
    } catch (const beam::TransferError& error) {
        // Structured connection closure must preserve the receiver's reason even
        // while this sender is still transmitting the payload.
        if (mode == "checksum" || mode == "truncated" || mode == "oversized" ||
            mode == "wrong-id") {
            const auto expected = mode == "checksum"   ? beam::TransferErrorCode::integrity
                                  : mode == "wrong-id" ? beam::TransferErrorCode::protocol
                                                       : beam::TransferErrorCode::size;
            if (!error.remote || error.code != expected) {
                std::cerr << "incorrect peer failure category: " << error.what() << '\n';
                return 1;
            }
        }
    }
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    if (argc > 1)
        return attack(argc, argv);
    int failures = 0;
    const auto check = [&](bool condition, const char* label) {
        if (!condition) {
            std::cerr << "FAIL: " << label << '\n';
            ++failures;
        }
    };
    const auto rejects = [&](auto function, const char* label) {
        try {
            function();
            check(false, label);
        } catch (const std::runtime_error&) {
        }
    };
    const auto rejects_with = [&](auto function, beam::TransferErrorCode code, const char* label) {
        try {
            function();
            check(false, label);
        } catch (const beam::TransferError& error) {
            check(error.code == code, label);
        }
    };
    const beam::protocol::Message offer{beam::protocol::Type::offer, 17, 123, {}, "hello.txt"};
    const auto encoded = beam::protocol::encode(offer);
    const auto decoded = beam::protocol::decode(encoded);
    check(decoded.id == 17 && decoded.size == 123 && decoded.text == "hello.txt",
          "offer roundtrip");
    for (unsigned code = 1; code <= 10; ++code) {
        const beam::protocol::Message failure{beam::protocol::Type::failed,
                                              17,
                                              0,
                                              {},
                                              "failed",
                                              static_cast<beam::TransferErrorCode>(code)};
        const auto frame = beam::protocol::encode(failure);
        Fragmented fragments(frame);
        check(beam::protocol::read(fragments).error == failure.error,
              "fragmented structured failure code");
        for (std::size_t length = 0; length < frame.size(); ++length)
            rejects_with([&] { beam::protocol::decode(std::span{frame}.first(length)); },
                         beam::TransferErrorCode::protocol, "truncated structured failure");
    }
    beam::protocol::Message failure{beam::protocol::Type::reject,     17, 0, {}, "refused",
                                    beam::TransferErrorCode::rejected};
    auto bad_code = beam::protocol::encode(failure);
    bad_code[14] = std::byte{255};
    rejects_with([&] { beam::protocol::decode(bad_code); }, beam::TransferErrorCode::protocol,
                 "unknown wire error code");
    failure.error = beam::TransferErrorCode::none;
    rejects_with([&] { beam::protocol::encode(failure); }, beam::TransferErrorCode::protocol,
                 "failure requires nonzero code");
    auto invalid_offer = offer;
    invalid_offer.error = beam::TransferErrorCode::cancelled;
    rejects_with([&] { beam::protocol::encode(invalid_offer); }, beam::TransferErrorCode::protocol,
                 "offer rejects error code");
    failure.error = beam::TransferErrorCode::rejected;
    failure.text = std::string(256, 'x');
    check(beam::protocol::decode(beam::protocol::encode(failure)).text.size() == 256,
          "maximum error text accepted");
    failure.text.push_back('x');
    rejects([&] { beam::protocol::encode(failure); }, "overlong error text rejected");
    failure.text = "bad\ntext";
    rejects([&] { beam::protocol::encode(failure); }, "error control characters rejected");
    // Invalid headers fail before waiting for a declared payload.
    auto bad_header = std::vector(encoded.begin(), encoded.begin() + 6);
    bad_header[0] = std::byte{1};
    Fragmented obsolete(bad_header);
    rejects_with([&] { beam::protocol::read(obsolete); }, beam::TransferErrorCode::protocol,
                 "obsolete version rejected before payload");
    check(obsolete.position == 6, "obsolete header consumes no payload");
    // Cancellation remains responsive during a blocked wait; spurious wakeups
    // must not extend the operation deadline.
    {
        std::mutex mutex;
        std::condition_variable changed;
        beam::TransferOptions options{};
        options.timeout = std::chrono::milliseconds(100);
        std::stop_source stop;
        options.stop_token = stop.get_token();
        std::jthread cancel([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            stop.request_stop();
        });
        const auto start = std::chrono::steady_clock::now();
        std::unique_lock lock(mutex);
        rejects_with(
            [&] {
                beam::transport::wait_for_operation(
                    changed, lock, options, [] { return false; }, "test timeout");
            },
            beam::TransferErrorCode::cancelled, "stop token wakes blocked wait");
        check(std::chrono::steady_clock::now() - start < std::chrono::seconds(1),
              "cancellation is bounded");
        options.stop_token = {};
        std::jthread wake([&](std::stop_token token) {
            while (!token.stop_requested()) {
                changed.notify_all();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        const auto timeout_start = std::chrono::steady_clock::now();
        rejects_with(
            [&] {
                beam::transport::wait_for_operation(
                    changed, lock, options, [] { return false; }, "test timeout");
            },
            beam::TransferErrorCode::timeout, "deadline survives spurious wakeups");
        check(std::chrono::steady_clock::now() - timeout_start < std::chrono::seconds(1),
              "spurious wakeups do not reset deadline");
        options.should_cancel = [] { return true; };
        rejects_with([&] { beam::transport::check_cancel(options); },
                     beam::TransferErrorCode::cancelled, "signal predicate cancellation");
    }
    Fragmented fragmented(encoded);
    check(beam::protocol::read(fragmented).text == "hello.txt", "byte-fragmented control frame");
    for (std::size_t length = 0; length < encoded.size(); ++length)
        rejects([&] { beam::protocol::decode(std::span{encoded}.first(length)); },
                "all truncated offer lengths");
    for (const auto type : {beam::protocol::Type::accept, beam::protocol::Type::complete,
                            beam::protocol::Type::acknowledged, beam::protocol::Type::reject,
                            beam::protocol::Type::failed}) {
        beam::protocol::Message message{type, 17, 0, {}, {}};
        if (type == beam::protocol::Type::reject || type == beam::protocol::Type::failed) {
            message.text = "refused";
            message.error = beam::TransferErrorCode::rejected;
        }
        check(beam::protocol::decode(beam::protocol::encode(message)).type == type,
              "control message roundtrip");
    }
    auto invalid = encoded;
    invalid[0] = std::byte{1};
    rejects([&] { beam::protocol::decode(invalid); }, "unknown version");
    invalid = encoded;
    invalid[1] = std::byte{99};
    rejects([&] { beam::protocol::decode(invalid); }, "unknown message type");
    invalid = encoded;
    invalid[2] = std::byte{255};
    Fragmented oversized(invalid);
    rejects([&] { beam::protocol::read(oversized); }, "oversized frame before payload allocation");
    invalid = encoded;
    invalid.push_back(std::byte{0});
    rejects([&] { beam::protocol::decode(invalid); }, "trailing frame bytes");
    auto unsafe = offer;
    unsafe.text = "../../escape";
    rejects([&] { beam::protocol::encode(unsafe); }, "unsafe offer filename");
    unsafe = offer;
    unsafe.id = 0;
    rejects([&] { beam::protocol::encode(unsafe); }, "zero id");
    const auto endpoint = beam::parse_endpoint("127.0.0.1:4269");
    check(endpoint.host == "127.0.0.1" && endpoint.port == 4269, "IPv4 endpoint");
    check(beam::parse_endpoint("[::1]:4269").host == "::1", "IPv6 endpoint");
    for (const auto* text :
         {"", ":4269", "127.0.0.1:0", "127.0.0.1:65536", "127.0.0.1:12x", "::1:4269", "[]:4269"})
        rejects([&] { (void)beam::parse_endpoint(text); }, "invalid endpoint");
    beam::transfer::Hash hash;
    const std::array abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    hash.update(abc);
    const auto digest = hash.finish();
    std::string hex;
    constexpr char digits[] = "0123456789abcdef";
    for (auto byte : digest) {
        const auto value = std::to_integer<unsigned>(byte);
        hex += digits[value >> 4];
        hex += digits[value & 15];
    }
    check(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "standard SHA-256 abc vector");
    const auto root = std::filesystem::temp_directory_path() /
                      ("beam-tests-" + std::to_string(beam::transfer::random_id()));
    std::filesystem::create_directory(root);
    try {
        {
            beam::transfer::Destination output(root, "verified.bin");
            output.write(abc);
            output.commit();
        }
        check(std::filesystem::file_size(root / "verified.bin") == 3, "verified file committed");
        rejects([&] { beam::transfer::Destination output(root, "verified.bin"); },
                "existing file protected");
        {
            beam::transfer::Destination output(root, "partial.bin");
            output.write(abc);
        }
        check(!std::filesystem::exists(root / "partial.bin"), "partial file never published");
        std::filesystem::create_symlink(root / "verified.bin", root / "link.bin");
        rejects([&] { beam::transfer::Destination output(root, "link.bin"); },
                "destination symlink protected");
        rejects([&] { beam::transfer::Source source(root / "link.bin"); },
                "source symlink rejected");
        rejects([&] { beam::transfer::Source source(root); }, "source directory rejected");
        rejects([&] { beam::transfer::Destination output(root, "../escape"); },
                "path traversal rejected");
        {
            beam::transfer::Destination output(root, "race.bin");
            std::ofstream(root / "race.bin") << "keep";
            rejects([&] { output.commit(); }, "collision at commit never overwrites");
            check(std::filesystem::file_size(root / "race.bin") == 4, "collision data preserved");
        }
        std::filesystem::create_directory_symlink(root, root / "linked-directory");
        rejects([&] { beam::transfer::Destination output(root / "linked-directory", "bad.bin"); },
                "symlink receive directory rejected");
        std::filesystem::create_directory(root / "selected");
        {
            beam::transfer::ReceiveDirectory selected(root / "selected");
            std::filesystem::rename(root / "selected", root / "moved");
            std::filesystem::create_directory(root / "selected");
            beam::transfer::Destination output(selected, "pinned.bin");
            output.write(abc);
            output.commit();
            check(std::filesystem::exists(root / "moved" / "pinned.bin"),
                  "held receive directory survives rename");
            check(std::filesystem::is_empty(root / "selected"),
                  "replacement receive directory untouched");
        }
        for (const auto& entry : std::filesystem::directory_iterator(root))
            check(!entry.path().filename().string().starts_with(".beam-"),
                  "temporary files cleaned");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ++failures;
    }
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
