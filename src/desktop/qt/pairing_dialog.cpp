#include "window.hpp"
#include <QApplication>
#include <QClipboard>
#include <QSysInfo>

namespace beam::desktop {
void Window::open_pairing() {
    attempt([&] {
#ifdef BEAM_HAS_PAIRING
        if (service.is_sending() || service.is_checking())
            throw std::logic_error("Wait for the active transfer before pairing");
        if (pairing)
            return;
        pairing = std::make_unique<PairingSession>(
            PairingOptions{config.state_directory,
                           QSysInfo::machineHostName().toUtf8().toStdString(), config.port});
        values["pairingOpen"] = true;
        try {
            pairing->host();
        } catch (...) {
            pairing.reset();
            values["pairingOpen"] = false;
            throw;
        }
        previous_pairing = PairingState::idle;
        poll_pairing();
#else
        throw std::logic_error("Network pairing is disabled in this build");
#endif
    });
}
void Window::close_pairing() {
#ifdef BEAM_HAS_PAIRING
    pairing.reset(); // Cancels and joins before the invitation disappears.
#endif
    values["pairingOpen"] = false;
    values["pairing"] = QVariantMap{};
    refresh_peers();
}
void Window::pairing_action(const QString& action, const QString& value) {
    attempt([&] {
#ifdef BEAM_HAS_PAIRING
        if (!pairing)
            throw std::logic_error("Open Add device first");
        if (action == "copy") {
            const auto snapshot = pairing->snapshot();
            if (snapshot.state == PairingState::hosting)
                QApplication::clipboard()->setText(QString::fromStdString(snapshot.invitation));
        } else if (action == "join") {
            pairing->join(value.trimmed().toUtf8().toStdString());
        } else if (action == "approve") {
            service.stop_receiving();
            pairing->approve(value.toUtf8().toStdString());
        } else if (action == "reject") {
            pairing->reject();
        } else if (action == "host") {
            pairing->host();
        } else if (action == "forget") {
            pairing->forget_incomplete();
        } else {
            throw std::invalid_argument("Unknown pairing action");
        }
        poll_pairing();
#else
        Q_UNUSED(action)
        Q_UNUSED(value)
        throw std::logic_error("Network pairing is disabled in this build");
#endif
    });
}
void Window::poll_pairing() {
#ifdef BEAM_HAS_PAIRING
    if (!pairing)
        return;
    const auto snapshot = pairing->snapshot();
    QStringList rows;
    for (const auto& row : snapshot.qr)
        rows.append(QString::fromStdString(row));
    values["pairing"] =
        QVariantMap{{"state", QString::fromLatin1(pairing_state_name(snapshot.state))},
                    {"message", QString::fromStdString(snapshot.message)},
                    {"remoteName", QString::fromStdString(snapshot.remote_name)},
                    {"alias", QString::fromStdString(snapshot.alias)},
                    {"qr", rows}};
    if (snapshot.state != previous_pairing) {
        previous_pairing = snapshot.state;
        if (snapshot.state == PairingState::paired)
            refresh_peers();
    }
#endif
}
} // namespace beam::desktop
