#include "session.hpp"
#include "beam/identity.hpp"

namespace beam::android {
Session::Session(const std::filesystem::path& directory, std::uint16_t port)
    : state(directory), service({directory, port, false}) {
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
        expected = 2;
        break;
    case Command::unpair:
    case Command::receive:
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
    case Command::peers: {
        std::vector<std::string> result;
        for (const auto& peer : IdentityStore(state).peers()) {
            result.insert(result.end(), {peer.name, peer.endpoint, peer.identity.fingerprint});
        }
        return result;
    }
    case Command::pair:
        if (service.is_receiving() || service.is_sending())
            throw std::logic_error("Stop transfers before editing paired devices");
        IdentityStore(state).pair(arguments[0], arguments[1], arguments[2], arguments[3]);
        break;
    case Command::unpair:
    case Command::endpoint:
        // Revocation cannot leave a listener using an earlier trust snapshot.
        if (service.is_receiving() || service.is_sending())
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
            kind = ContentKind::url;
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
        break;
    case Command::stop:
        service.stop_receiving();
        break;
    case Command::events: {
        std::vector<std::string> result;
        for (const auto& event : service.take_events()) {
            result.insert(result.end(),
                          {std::to_string(static_cast<int>(event.direction)),
                           std::to_string(static_cast<int>(event.kind)), event.message,
                           event.result.filename, std::to_string(event.result.bytes),
                           std::to_string(static_cast<int>(event.progress.stage)),
                           std::to_string(event.progress.bytes),
                           std::to_string(event.progress.total)});
        }
        return result;
    }
    case Command::busy:
        return {service.is_sending() ? "1" : "0", service.is_receiving() ? "1" : "0"};
    default:
        throw std::invalid_argument("Unknown Android command");
    }
    return {};
}
} // namespace beam::android
