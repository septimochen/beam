#include "beam/identity.hpp"
#include "protocol/codec.hpp"
#include "transfer/files.hpp"
#include <array>
#include <iostream>
#include <thread>

namespace {
void require(bool condition, const char* label) {
    if (!condition)
        throw std::logic_error(label);
}
template <typename Error = std::invalid_argument, typename Action> void rejects(Action action) {
    try {
        action();
    } catch (const Error&) {
        return;
    }
    throw std::logic_error("invalid content was accepted");
}
class Fragmented final : public beam::transport::Stream {
    std::vector<std::byte> bytes;
    std::size_t offset = 0;

  public:
    explicit Fragmented(std::vector<std::byte> data) : bytes(std::move(data)) {}
    void send(std::span<const std::byte>) override { throw std::logic_error("unexpected send"); }
    void finish() override {}
    std::size_t read(std::span<std::byte> output) override {
        if (offset == bytes.size() || output.empty())
            return 0;
        output[0] = bytes[offset++];
        return 1;
    }
};
void unit_tests() {
    using beam::ContentKind;
    for (const auto text : {"", "hello", "你好 🌍", "first\nsecond\r\n\tthird"})
        beam::validate_content(ContentKind::text, text);
    beam::validate_content(ContentKind::text, std::string(beam::max_text_size, 'x'));
    for (const auto& text :
         {std::string("a\0b", 3), std::string("\x1b[31m"), std::string("\x7f"),
          std::string("\xc2\x80"), std::string("\x80"), std::string("\xc0\xaf"),
          std::string("\xe0\x80\xaf"), std::string("\xed\xa0\x80"), std::string("\xf0\x80\x80\x80"),
          std::string("\xf4\x90\x80\x80"), std::string("\xf5\x80\x80\x80"), std::string("\xe2\x82"),
          std::string(beam::max_text_size + 1, 'x')})
        rejects([&] { beam::validate_content(ContentKind::text, text); });
    for (const auto text : {"https://example.com", "file:///etc/passwd", "javascript:alert(1)",
                            "https://x/你好 has spaces"})
        beam::validate_content(ContentKind::text, text);
    rejects([&] { beam::validate_content(ContentKind::file, "hello"); });
    require(beam::content_type(ContentKind::text) == "text/plain",
            "incorrect content type mapping");
    for (const auto kind : {ContentKind::text}) {
        beam::protocol::Message offer{beam::protocol::Type::offer,   42,  8, {}, {},
                                      beam::TransferErrorCode::none, kind};
        const auto frame = beam::protocol::encode(offer);
        Fragmented stream(frame);
        const auto decoded = beam::protocol::read(stream);
        require(decoded.kind == kind && decoded.size == 8 && decoded.text.empty(),
                "typed offer roundtrip failed");
        for (std::size_t length = 0; length < frame.size(); ++length)
            rejects<beam::TransferError>(
                [&] { beam::protocol::decode(std::span{frame}.first(length)); });
        auto invalid = frame;
        invalid[14] = std::byte{3};
        rejects<beam::TransferError>([&] { beam::protocol::decode(invalid); });
        invalid = frame;
        invalid[14] = std::byte{99};
        rejects<beam::TransferError>([&] { beam::protocol::decode(invalid); });
        invalid = frame;
        invalid[0] = std::byte{3};
        rejects<beam::TransferError>([&] { beam::protocol::decode(invalid); });
        offer.text = "disguised.txt";
        rejects<beam::TransferError>([&] { beam::protocol::encode(offer); });
        offer.text.clear();
        offer.size = beam::max_text_size;
        beam::protocol::encode(offer);
        ++offer.size;
        rejects<beam::TransferError>([&] { beam::protocol::encode(offer); });
        offer.size = 0;
        beam::protocol::encode(offer);
    }
}
void runtime_tests(int argc, char** argv) {
    require(argc == 6, "expected mode, state directory, peer, endpoint and output");
    const std::string mode = argv[1];
    beam::TransferOptions options;
    std::string server_name;
    {
        beam::IdentityStore store(argv[2]);
        options.credentials = store.credentials(argv[3]);
        server_name = store.peer(argv[3]).identity.tls_name;
    }
    options.timeout = std::chrono::seconds(3);
    const auto endpoint = beam::parse_endpoint(argv[4]);
    std::stop_source stop;
    options.stop_token = stop.get_token();
    const auto caller = std::this_thread::get_id();
    bool complete = false, verifying = false;
    std::string snapshot = "immutable original";
    options.should_cancel = [&] {
        if (mode == "snapshot")
            snapshot.assign("changed by cancellation predicate");
        return false;
    };
    options.on_listening = [] { std::cout << "Listening" << std::endl; };
    options.on_progress = [&](const beam::TransferProgress& update) {
        require(std::this_thread::get_id() == caller && update.bytes <= update.total,
                "invalid progress thread or count");
        if (mode == "snapshot")
            snapshot.assign("changed by callback");
        if (update.stage == beam::TransferStage::verifying)
            verifying = true;
        if (update.stage == beam::TransferStage::complete) {
            require(verifying, "premature completion");
            complete = true;
        }
        if ((mode == "cancel-hash" && update.stage == beam::TransferStage::hashing) ||
            ((mode == "cancel-send" || mode == "cancel-receive") &&
             update.stage == beam::TransferStage::transferring && update.bytes > 0) ||
            (mode == "cancel-verify" && update.stage == beam::TransferStage::verifying))
            stop.request_stop();
        if (mode == "callback-error" && update.stage == beam::TransferStage::transferring &&
            update.bytes > 0)
            throw std::logic_error("callback failed");
    };
    if (mode == "file-only" || mode == "cancel-receive" || mode == "cancel-verify" ||
        mode == "callback-error") {
        try {
            if (mode == "file-only")
                beam::receive_file(endpoint.port, argv[5], options);
            else
                beam::receive_content(endpoint.port, argv[5], options);
            throw std::logic_error("failed receive reported success");
        } catch (const beam::TransferError& error) {
            require(error.code == (mode == "file-only" ? beam::TransferErrorCode::rejected
                                                       : beam::TransferErrorCode::cancelled) &&
                        !error.remote && !complete,
                    "incorrect receive failure");
        } catch (const std::logic_error& error) {
            require(mode == "callback-error" && std::string_view(error.what()) == "callback failed",
                    "incorrect callback failure");
        }
        return;
    }
    if (mode == "max-text" || mode == "url-text" || mode == "snapshot" || mode == "cancel-hash" ||
        mode == "cancel-send") {
        try {
            const auto result =
                mode == "snapshot"
                    ? beam::send_text(endpoint, std::string_view(snapshot), server_name, options)
                : mode == "url-text"
                    ? beam::send_text(endpoint,
                                      "https://example.com/" +
                                          std::string(beam::max_text_size - 20, 'x'),
                                      server_name, options)
                    : beam::send_text(endpoint, std::string(beam::max_text_size, 'x'), server_name,
                                      options);
            require(!mode.starts_with("cancel-") && complete &&
                        result.kind == beam::ContentKind::text,
                    "incorrect typed success result");
        } catch (const beam::TransferError& error) {
            require(mode.starts_with("cancel-") &&
                        error.code == beam::TransferErrorCode::cancelled && !error.remote &&
                        !complete,
                    "incorrect sender cancellation");
        }
        return;
    }
    // Authenticated adversarial sender: bypass public content validation, retaining
    // a correct digest to prove validation is independent of integrity checking.
    std::string content = "你好";
    if (mode == "invalid-utf8")
        content = "\xed\xa0\x80";
    if (mode == "nul-text")
        content = std::string("a\0b", 3);
    const auto bytes = std::as_bytes(std::span{content.data(), content.size()});
    beam::transfer::Hash hash;
    hash.update(bytes);
    beam::protocol::Message offer{beam::protocol::Type::offer,
                                  42,
                                  bytes.size(),
                                  hash.finish(),
                                  {},
                                  beam::TransferErrorCode::none,
                                  beam::ContentKind::text};
    if (mode == "checksum")
        offer.digest = {};
    if (mode == "truncated")
        ++offer.size;
    if (mode == "oversized")
        --offer.size;
    auto frame = beam::protocol::encode(offer);
    if (mode == "unknown-kind")
        frame[14] = std::byte{99};
    if (mode == "oversize-metadata")
        frame[15] = std::byte{1};
    if (mode == "obsolete")
        frame[0] = std::byte{2};
    auto connection = beam::transport::connect(endpoint, server_name, options);
    std::unique_ptr<beam::transport::Stream> control, payload;
    try {
        control = connection->open(false);
        control->send(frame);
        const auto response = beam::protocol::read(*control);
        if (response.type == beam::protocol::Type::reject) {
            require(mode == "collision" &&
                        response.error == beam::TransferErrorCode::destination_exists,
                    "unexpected offer rejection");
            return;
        }
        require(response.type == beam::protocol::Type::accept, "expected acceptance");
        payload = connection->open(true);
        payload->send(beam::protocol::encode_id(mode == "wrong-id" ? 43 : 42));
        // Once an invalid ID is sent, wait for the rejection instead of racing
        // more StreamSend calls against a handle the peer has already shut down.
        if (mode != "wrong-id") {
            if (mode == "fragmented-text") {
                // Split UTF-8 code points across independent sends.
                for (const auto byte : bytes) {
                    const std::array part{byte};
                    payload->send(part);
                }
            } else {
                payload->send(bytes);
            }
            payload->finish();
        }
        const auto result = beam::protocol::read(*control);
        require(result.type == beam::protocol::Type::complete &&
                    (mode == "fragmented-text" || mode == "hold-ack"),
                "invalid content was published");
        if (mode == "hold-ack") {
            std::cout << "Paused" << std::endl;
            (void)beam::protocol::read(*control);
            throw std::logic_error("held completion unexpectedly succeeded");
        }
        beam::protocol::write(*control, {beam::protocol::Type::acknowledged, offer.id, 0, {}, {}});
        control->finish();
        std::array<std::byte, 1> trailing{};
        require(control->read(trailing) == 0, "unexpected final bytes");
    } catch (const beam::TransferError& error) {
        const auto expected = mode == "checksum" ? beam::TransferErrorCode::integrity
                              : mode == "truncated" || mode == "oversized"
                                  ? beam::TransferErrorCode::size
                              : mode == "collision" ? beam::TransferErrorCode::destination_exists
                              : mode == "hold-ack"  ? beam::TransferErrorCode::cancelled
                                                    : beam::TransferErrorCode::protocol;
        if (mode == "fragmented-text" || !error.remote || error.code != expected)
            throw std::logic_error(mode + ": incorrect peer rejection category: " + error.what());
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 1)
            unit_tests();
        else
            runtime_tests(argc, argv);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
