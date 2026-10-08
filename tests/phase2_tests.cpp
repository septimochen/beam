#include "beam/transfer.hpp"
#include "protocol/codec.hpp"
#include "transfer/files.hpp"
#include "transport/transport.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::logic_error(message);
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 7)
        return 2;
    try {
        const std::string mode = argv[1];
        const auto endpoint = beam::parse_endpoint(argv[2]);
        beam::TransferOptions options{{argv[3], argv[4], argv[5]}, std::chrono::seconds(5)};
        std::stop_source stop;
        options.stop_token = stop.get_token();
        const auto caller = std::this_thread::get_id();
        std::vector<beam::TransferProgress> updates;
        options.on_progress = [&](const beam::TransferProgress& update) {
            require(caller == std::this_thread::get_id(), "progress changed threads");
            require(update.bytes <= update.total, "progress exceeds offered size");
            if (!updates.empty() && updates.back().stage == update.stage) {
                require(update.bytes >= updates.back().bytes, "progress went backwards");
                require(update.total == updates.back().total, "progress total changed");
            }
            updates.push_back(update);
            if (update.stage == beam::TransferStage::connecting) {
                if (mode == "grow-source")
                    std::ofstream(argv[6], std::ios::app) << 'x';
                if (mode == "shrink-source")
                    std::filesystem::resize_file(argv[6], 0);
            }
            if ((mode == "cancel-hash" && update.stage == beam::TransferStage::hashing &&
                 update.bytes > 0) ||
                ((mode == "cancel-send" || mode == "cancel-receive") &&
                 update.stage == beam::TransferStage::transferring && update.bytes > 0) ||
                (mode == "cancel-verify" && update.stage == beam::TransferStage::verifying))
                stop.request_stop();
            if (mode == "callback-error" && update.stage == beam::TransferStage::transferring &&
                update.bytes > 0)
                throw std::logic_error("callback failed");
        };
        options.on_listening = [] { std::cout << "Listening" << std::endl; };
        if (mode == "hold-accept" || mode == "wrong-response-id" ||
            mode == "wrong-response-state" || mode == "hold-read") {
            auto connection = beam::transport::listen(endpoint.port, options);
            auto control = connection->accept(false);
            const auto offer = beam::protocol::read(*control);
            if (mode == "hold-read") {
                beam::protocol::write(*control,
                                      {beam::protocol::Type::accept, offer.id, 0, {}, {}});
                auto payload = connection->accept(true);
                std::array<std::byte, 8> id{};
                beam::transport::read_exact(*payload, id);
                std::cout << "Paused" << std::endl;
                try {
                    (void)beam::protocol::read(*control);
                } catch (const beam::TransferError& error) {
                    require(error.remote && error.code == beam::TransferErrorCode::cancelled,
                            "blocked sender did not propagate cancellation");
                    return 0;
                }
                throw std::logic_error("sender sent unexpected control data");
            }
            if (mode == "hold-accept") {
                std::cout << "Paused" << std::endl;
                try {
                    (void)beam::protocol::read(*control);
                } catch (const beam::TransferError& error) {
                    require(error.remote && error.code == beam::TransferErrorCode::cancelled,
                            "waiting sender did not propagate cancellation");
                    return 0;
                }
                throw std::logic_error("sender sent unexpected control data");
            }
            try {
                beam::protocol::write(*control,
                                      {mode == "wrong-response-id" ? beam::protocol::Type::accept
                                                                   : beam::protocol::Type::complete,
                                       offer.id + (mode == "wrong-response-id" ? 1U : 0U),
                                       0,
                                       {},
                                       {}});
                (void)beam::protocol::read(*control);
            } catch (const beam::TransferError&) {
            }
            return 0;
        }
        if (mode == "hold-payload" || mode == "hold-ack") {
            auto connection = beam::transport::connect(endpoint, "localhost", options);
            auto control = connection->open(false);
            const std::array bytes{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
            beam::transfer::Hash hash;
            hash.update(bytes);
            beam::protocol::write(*control,
                                  {beam::protocol::Type::offer, 42, 3, hash.finish(), "held.bin"});
            require(beam::protocol::read(*control).type == beam::protocol::Type::accept,
                    "offer rejected");
            auto payload = connection->open(true);
            payload->send(beam::protocol::encode_id(42));
            payload->send(mode == "hold-ack" ? std::span{bytes} : std::span{bytes}.first(1));
            if (mode == "hold-ack") {
                payload->finish();
                require(beam::protocol::read(*control).type == beam::protocol::Type::complete,
                        "verified payload did not complete");
            }
            std::cout << "Paused" << std::endl;
            try {
                (void)beam::protocol::read(*control);
            } catch (const beam::TransferError& error) {
                require(error.remote && error.code == beam::TransferErrorCode::cancelled,
                        "receiver did not propagate cancellation");
                return 0;
            }
            throw std::logic_error("receiver sent unexpected control data");
        }
        if (mode == "cancel-before")
            stop.request_stop();
        try {
            const bool receive = mode == "progress-receive" || mode == "cancel-receive" ||
                                 mode == "cancel-verify" || mode == "callback-error";
            const auto result = receive ? beam::receive_file(endpoint.port, argv[6], options)
                                        : beam::send_file(endpoint, argv[6], "localhost", options);
            require(mode == "progress-send" || mode == "progress-receive",
                    "cancelled transfer succeeded");
            require(!updates.empty() && updates.back().stage == beam::TransferStage::complete,
                    "missing verified completion progress");
            require(updates.back().bytes == result.bytes && updates.back().total == result.bytes,
                    "completion byte count incorrect");
            bool transfer_started = false, verifying = false;
            for (const auto& update : updates) {
                if (update.stage == beam::TransferStage::transferring) {
                    if (!transfer_started)
                        require(update.bytes == 0, "missing zero initial progress");
                    transfer_started = true;
                }
                if (update.stage == beam::TransferStage::verifying) {
                    require(transfer_started && update.bytes == result.bytes,
                            "premature verification");
                    verifying = true;
                }
                if (update.stage == beam::TransferStage::complete)
                    require(verifying, "premature completion");
            }
            return 0;
        } catch (const beam::TransferError& error) {
            if (mode == "grow-source" || mode == "shrink-source") {
                require(error.code == beam::TransferErrorCode::source_changed && !error.remote,
                        "incorrect source-change error");
                return 0;
            }
            require(mode.starts_with("cancel-"), "unexpected transfer error");
            require(error.code == beam::TransferErrorCode::cancelled && !error.remote,
                    "incorrect local cancellation error");
            for (const auto& update : updates)
                require(update.stage != beam::TransferStage::complete,
                        "cancelled transfer reported complete");
            return 0;
        } catch (const std::logic_error& error) {
            if (mode == "callback-error" && std::string_view(error.what()) == "callback failed")
                return 0;
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
