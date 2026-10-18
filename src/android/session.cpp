#include "session.hpp"
#include "beam/identity.hpp"

namespace beam::android {
Session::Session(const std::filesystem::path& directory, std::uint16_t port,
                 std::chrono::milliseconds timeout)
    : state(directory), service({directory, port, false, timeout})
#ifdef BEAM_HAS_PAIRING
      ,
      transfer_port(port)
#endif
{
    IdentityStore store(state, true);
    store.initialize();
}
std::vector<std::string> Session::call(Command command, const std::vector<std::string>& arguments) {
    std::size_t expected = 0;
    switch (command) {
    case Command::pair:
    case Command::send:
        expected = 4;
        break;
    case Command::endpoint:
    case Command::pairing_pin:
        expected = 2;
        break;
    case Command::unpair:
    case Command::receive:
    case Command::check:
    case Command::pairing_host:
    case Command::pairing_join:
    case Command::pairing_approve:
        expected = 1;
        break;
    default:
        break;
    }
    if (arguments.size() != expected)
        throw std::invalid_argument("Invalid Android command arguments");
    for (const auto& argument : arguments) {
        if (argument.size() > max_text_size || argument.find('\0') != std::string::npos)
            throw std::invalid_argument("Android argument exceeds bounds or contains NUL");
    }
    switch (command) {
    case Command::identity: {
        const auto identity = IdentityStore(state).identity();
        return {identity.fingerprint, identity.tls_name, identity.certificate_pem};
    }
#ifdef BEAM_HAS_PAIRING
    case Command::pairing_host:
        if (service.is_sending() || service.is_checking())
            throw std::logic_error("Wait for the active transfer before pairing");
        pairing =
            std::make_unique<PairingSession>(PairingOptions{state, arguments[0], transfer_port});
        pairing->host();
        break;
    case Command::pairing_join:
    case Command::pairing_pin:
    case Command::pairing_approve:
    case Command::pairing_snapshot:
    case Command::pairing_browse:
    case Command::pairing_cancel:
    case Command::pairing_forget:
    case Command::pairing_reject:
        if (!pairing) {
            if (command == Command::pairing_cancel)
                break;
            pairing = std::make_unique<PairingSession>(
                PairingOptions{state, "Beam Android", transfer_port});
        }
        if (command == Command::pairing_join)
            pairing->join(arguments[0]);
        else if (command == Command::pairing_pin)
            pairing->join_pin(arguments[0], arguments[1]);
        else if (command == Command::pairing_approve) {
            if (service.is_sending() || service.is_checking())
                throw std::logic_error("Wait for the active transfer before pairing");
            service.stop_receiving();
            pairing->approve(arguments[0]);
        } else if (command == Command::pairing_browse)
            pairing->browse();
        else if (command == Command::pairing_cancel)
            pairing->cancel();
        else if (command == Command::pairing_forget)
            pairing->forget_incomplete();
        else if (command == Command::pairing_reject)
            pairing->reject();
        else {
            const auto snapshot = pairing->snapshot();
            std::string rows;
            for (const auto& row : snapshot.qr)
                rows += row + "\n";
            std::vector<std::string> result{pairing_state_name(snapshot.state),
                                            snapshot.message,
                                            snapshot.invitation,
                                            snapshot.pin,
                                            snapshot.remote_name,
                                            snapshot.alias,
                                            std::to_string(snapshot.port),
                                            rows};
            for (const auto& device : snapshot.nearby)
                result.insert(result.end(), {device.name, device.endpoint});
            return result;
        }
        break;
#endif
    case Command::peers: {
        std::vector<std::string> result;
        for (const auto& peer : IdentityStore(state).peers()) {
            result.insert(result.end(), {peer.name, peer.endpoint, peer.identity.fingerprint});
        }
        return result;
    }
    case Command::pair:
        if (service.is_receiving() || service.is_sending() || service.is_checking())
            throw std::logic_error("Stop transfers before editing paired devices");
        IdentityStore(state).pair(arguments[0], arguments[1], arguments[2], arguments[3]);
        break;
    case Command::unpair:
    case Command::endpoint:
        // Revocation cannot leave a listener using an earlier trust snapshot.
        if (service.is_receiving() || service.is_sending() || service.is_checking())
            throw std::logic_error("Stop transfers before editing paired devices");
        if (command == Command::unpair)
            IdentityStore(state).unpair(arguments[0]);
        else
            IdentityStore(state).set_endpoint(arguments[0], arguments[1]);
        break;
    case Command::send: {
        ContentKind kind;
        if (arguments[1] == "file")
            kind = ContentKind::file;
        else if (arguments[1] == "text")
            kind = ContentKind::text;
        else if (arguments[1] == "url")
            throw std::invalid_argument("URL mode was removed; send URLs as text");
        else
            throw std::invalid_argument("Unsupported Android content kind");
        service.send({arguments[0], kind, arguments[2], arguments[3]});
        break;
    }
    case Command::receive:
        service.start_receiving(arguments[0]);
        break;
    case Command::cancel:
        service.cancel_send();
        service.cancel_check();
        break;
    case Command::stop:
        service.stop_receiving();
        break;
    case Command::events: {
        std::vector<std::string> result;
        for (const auto& event : service.take_events()) {
            result.insert(
                result.end(),
                {std::to_string(static_cast<int>(event.direction)),
                 std::to_string(static_cast<int>(event.kind)), event.message, event.result.filename,
                 std::to_string(event.result.bytes),
                 std::to_string(static_cast<int>(event.progress.stage)),
                 std::to_string(event.progress.bytes), std::to_string(event.progress.total),
                 std::to_string(event.progress.bytes_per_second),
                 std::to_string(static_cast<unsigned>(event.result.kind)), event.result.text});
        }
        return result;
    }
    case Command::busy:
        return {service.is_sending() ? "1" : "0", service.is_receiving() ? "1" : "0",
                service.is_checking() ? "1" : "0"};
    case Command::check:
        service.check_peer(arguments[0]);
        break;
    default:
        throw std::invalid_argument("Unknown Android command");
    }
    return {};
}
} // namespace beam::android
