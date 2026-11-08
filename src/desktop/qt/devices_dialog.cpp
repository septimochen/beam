#include "beam/identity.hpp"
#include "window.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>

namespace beam::desktop {
namespace {
QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
std::string bytes(const QString& value) { return value.toUtf8().toStdString(); }
} // namespace
void Window::begin_edit(const QString& name) {
    attempt([&] {
        editing = IdentityStore(config.state_directory).peer(bytes(name));
        values["editor"] = QVariantMap{{"name", text(editing->name)},
                                       {"endpoint", text(editing->endpoint)},
                                       {"fallback", text(editing->fallback_endpoint)},
                                       {"fingerprint", text(editing->identity.fingerprint)},
                                       {"certificate", text(editing->identity.certificate_pem)}};
        values["replacementCertificate"] = "";
    });
}
bool Window::save_device(const QVariantMap& fields) {
    bool saved = false;
    attempt([&] {
        if (!editing)
            throw std::logic_error("Select a device before editing");
        const bool replace = fields.value("replace").toBool();
        if (replace && (fields.value("certificate").toString().isEmpty() ||
                        fields.value("fingerprint").toString().size() != 64))
            throw std::invalid_argument(
                "Choose a certificate and verify all 64 fingerprint characters");
        try {
            service.edit_peer(editing->name,
                              {bytes(fields.value("name").toString()),
                               bytes(fields.value("endpoint").toString()),
                               bytes(fields.value("fallback").toString()),
                               replace ? bytes(fields.value("certificate").toString()) : "",
                               replace ? bytes(fields.value("fingerprint").toString()) : "",
                               editing->identity.fingerprint});
        } catch (...) {
            refresh_peers();
            throw;
        }
        editing.reset();
        refresh_peers();
        values["deviceStatus"] = "Saved device changes.";
        saved = true;
    });
    return saved;
}
void Window::choose_certificate() {
    attempt([&] {
        const auto path = QFileDialog::getOpenFileName(nullptr, "Choose public certificate");
        if (!path.isEmpty())
            values["replacementCertificate"] = text(read_device_backup(bytes(path)));
    });
}
void Window::unpair_device(const QString& name) {
    attempt([&] {
        try {
            service.unpair(bytes(name));
        } catch (...) {
            refresh_peers();
            throw;
        }
        refresh_peers();
        values["deviceStatus"] = "Device unpaired.";
    });
}
void Window::show_identity() {
    attempt([&] {
        const auto identity = IdentityStore(config.state_directory).identity();
        values["identity"] = QVariantMap{{"fingerprint", text(identity.fingerprint)},
                                         {"certificate", text(identity.certificate_pem)}};
    });
}
void Window::copy_certificate() {
    QApplication::clipboard()->setText(values["identity"].toMap().value("certificate").toString());
}
void Window::export_devices() {
    attempt([&] {
        const auto path = QFileDialog::getSaveFileName(nullptr, "Export paired devices",
                                                       "beam-devices.beam-backup");
        if (path.isEmpty())
            return;
        write_device_backup(bytes(path), IdentityStore(config.state_directory).export_devices());
        values["deviceStatus"] = "Device backup saved (public certificates only).";
    });
}
void Window::preview_import() {
    discard_import();
    attempt([&] {
        const auto path = QFileDialog::getOpenFileName(nullptr, "Import device backup");
        if (path.isEmpty())
            return;
        const auto data = read_device_backup(bytes(path));
        const auto preview = IdentityStore(config.state_directory).preview_import(data);
        QString details;
        for (const auto& peer : preview.peers)
            details += text(peer.name) + "\n" + text(peer.endpoint) +
                       "\nFallback: " + text(peer.fallback_endpoint) +
                       "\nSHA-256: " + text(peer.identity.fingerprint) + "\n\n";
        values["importPreview"] =
            QVariantMap{{"details", details},
                        {"summary", QString::number(preview.added) + " to add · " +
                                        QString::number(preview.existing) + " already paired"},
                        {"differentIdentity",
                         preview.source_fingerprint !=
                             IdentityStore(config.state_directory).identity().fingerprint}};
        import_contents = data;
    });
}
bool Window::restore_devices() {
    bool restored = false;
    attempt([&] {
        if (import_contents.empty())
            throw std::logic_error("Preview a backup before restoring");
        try {
            service.import_devices(import_contents);
        } catch (...) {
            refresh_peers();
            throw;
        }
        refresh_peers();
        discard_import();
        values["deviceStatus"] = "Device backup restored.";
        restored = true;
    });
    return restored;
}
void Window::discard_import() {
    import_contents.clear();
    values["importPreview"] = QVariantMap{};
    emit changed();
}
} // namespace beam::desktop
