#include "devices_dialog.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#ifdef BEAM_HAS_PAIRING
#include "pairing_dialog.hpp"
#endif

namespace beam::desktop {
namespace {
QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
std::string bytes(const QString& value) { return value.toUtf8().toStdString(); }
QLabel* label(const QString& value, QWidget* parent) {
    auto* result = new QLabel(value, parent);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}
bool edit_device(QWidget* parent, Service& service, const TrustedPeer& peer,
                 const std::function<void()>& changed) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Edit " + text(peer.name));
    dialog.resize(560, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll);
    auto* form = new QFormLayout(body);
    auto* name = new QLineEdit(text(peer.name), body);
    name->setObjectName("deviceName");
    name->setMaxLength(63);
    form->addRow("Device name", name);
    form->addRow(label("1–63 lowercase letters, digits, - or _; start with a letter.", body));
    auto* endpoint = new QLineEdit(text(peer.endpoint), body);
    endpoint->setObjectName("deviceEndpoint");
    endpoint->setMaxLength(256);
    endpoint->setPlaceholderText("192.168.1.20:4269 or [IPv6]:4269");
    form->addRow("IP address & port", endpoint);
    auto* tailscale = new QLineEdit(text(peer.tailscale_endpoint), body);
    tailscale->setObjectName("deviceTailscale");
    tailscale->setMaxLength(256);
    form->addRow("Tailscale (optional)", tailscale);
    form->addRow(label(
        "Leave an address empty to remove it. Saving stops active transfers and checks.", body));
    form->addRow("Current SHA-256", label(text(peer.identity.fingerprint), body));
    auto* current = new QPlainTextEdit(text(peer.identity.certificate_pem), body);
    current->setReadOnly(true);
    current->setMaximumHeight(90);
    current->setAccessibleName("Current public certificate");
    form->addRow("Public certificate", current);
    auto* replace = new QCheckBox("Replace certificate", body);
    replace->setObjectName("replaceCertificate");
    form->addRow(replace);
    auto* replacement = new QWidget(body);
    auto* cert_layout = new QVBoxLayout(replacement);
    cert_layout->setContentsMargins(0, 0, 0, 0);
    cert_layout->addWidget(label("This changes which identity you trust. Verify the full new "
                                 "fingerprint on the other device.",
                                 body));
    auto* choose = new QPushButton("Choose public certificate…", body);
    cert_layout->addWidget(choose);
    auto* certificate = new QPlainTextEdit(body);
    certificate->setAccessibleName("Replacement public certificate PEM");
    certificate->setMaximumHeight(100);
    cert_layout->addWidget(certificate);
    auto* fingerprint = new QLineEdit(body);
    fingerprint->setAccessibleName("Verified SHA-256");
    fingerprint->setMaxLength(64);
    fingerprint->setPlaceholderText("Independently verified SHA-256 (all 64 characters)");
    cert_layout->addWidget(fingerprint);
    replacement->hide();
    form->addRow(replacement);
    QObject::connect(replace, &QCheckBox::toggled, replacement, &QWidget::setVisible);
    scroll->setWidget(body);
    layout->addWidget(scroll);
    auto* error = label({}, &dialog);
    error->setObjectName("deviceError");
    layout->addWidget(error);
    QObject::connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto file = QFileDialog::getOpenFileName(&dialog, "Choose public certificate");
        if (file.isEmpty())
            return;
        try {
            certificate->setPlainText(text(read_device_backup(bytes(file))));
        } catch (const std::exception& failure) {
            error->setText(text(failure.what()));
        }
    });
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (replace->isChecked() &&
                (certificate->toPlainText().isEmpty() || fingerprint->text().size() != 64))
                throw std::invalid_argument(
                    "Choose a certificate and verify all 64 fingerprint characters");
            service.edit_peer(
                peer.name, {bytes(name->text()), bytes(endpoint->text()), bytes(tailscale->text()),
                            replace->isChecked() ? bytes(certificate->toPlainText()) : "",
                            replace->isChecked() ? bytes(fingerprint->text()) : "",
                            peer.identity.fingerprint});
            changed();
            dialog.accept();
        } catch (const std::exception& failure) {
            changed();
            error->setText(text(failure.what()));
        }
    });
    return dialog.exec() == QDialog::Accepted;
}
} // namespace
void show_devices_dialog(QWidget* parent, const Config& config, Service& service,
                         const std::function<void()>& changed, QLabel* connectivity) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Your devices — Beam");
    dialog.resize(720, 740);
    dialog.setObjectName("devicesDialog");
    dialog.setStyleSheet(
        "QFrame#deviceCard { border: 1px solid palette(mid); border-radius: 16px; background: "
        "palette(base); }"
        "QLineEdit#deviceSearch { padding: 10px; border: 1px solid palette(mid); border-radius: "
        "10px; }"
        "QPushButton { padding: 7px 12px; } QLabel#deviceError { color: #c14355; }");
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    auto* header = new QHBoxLayout;
    auto* heading = label("Your devices", &dialog);
    auto font = heading->font();
    font.setPointSize(26);
    font.setBold(true);
    heading->setFont(font);
    header->addWidget(heading);
    header->addStretch();
#ifdef BEAM_HAS_PAIRING
    auto* add = new QPushButton("+ Add device", &dialog);
    add->setObjectName("addDevice");
    header->addWidget(add);
#endif
    layout->addLayout(header);
    layout->addWidget(label("A trusted connection, wherever you work.", &dialog));
    auto* toolbar = new QHBoxLayout;
    auto* count = label({}, &dialog);
    toolbar->addWidget(count);
    toolbar->addStretch();
    auto* identity = new QPushButton("My identity", &dialog);
    toolbar->addWidget(identity);
    auto* refresh = new QPushButton("Refresh", &dialog);
    toolbar->addWidget(refresh);
    layout->addLayout(toolbar);
    auto* search = new QLineEdit(&dialog);
    search->setObjectName("deviceSearch");
    search->setPlaceholderText("Search devices or addresses");
    search->setAccessibleName("Search devices");
    layout->addWidget(search);
    auto* status = label({}, &dialog);
    status->setObjectName("deviceStatus");
    layout->addWidget(status);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* list = new QWidget(scroll);
    auto* cards = new QVBoxLayout(list);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(12);
    scroll->setWidget(list);
    layout->addWidget(scroll, 1);
    auto* backup = new QFrame(&dialog);
    backup->setObjectName("deviceCard");
    auto* backup_layout = new QVBoxLayout(backup);
    backup_layout->addWidget(label("Backup & restore", backup));
    backup_layout->addWidget(
        label("Keep your devices, addresses, and public certificates. Your private key stays here.",
              backup));
    auto* backup_actions = new QHBoxLayout;
    auto* export_button = new QPushButton("Export backup…", backup);
    auto* import_button = new QPushButton("Import backup…", backup);
    export_button->setObjectName("exportDevices");
    import_button->setObjectName("importDevices");
    backup_actions->addWidget(export_button);
    backup_actions->addWidget(import_button);
    backup_actions->addStretch();
    backup_layout->addLayout(backup_actions);
    layout->addWidget(backup);
    auto* done = new QPushButton("Done", &dialog);
    layout->addWidget(done, 0, Qt::AlignRight);
    QObject::connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
    const auto attempt = [&](const std::function<void()>& action) {
        try {
            status->clear();
            action();
        } catch (const std::exception& error) {
            status->setText(text(error.what()));
        }
    };
    QPointer<QLabel> checked_label;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (checked_label)
            checked_label->setText(connectivity->text());
    });
    timer.start(100);
    std::function<void()> render;
    render = [&] {
        while (auto* item = cards->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const auto devices = IdentityStore(config.state_directory).peers();
        count->setText(QString::number(devices.size()) + " paired devices");
        int visible = 0;
        for (const auto& device : devices) {
            const auto query = search->text();
            if (!query.isEmpty() && !text(device.name).contains(query, Qt::CaseInsensitive) &&
                !text(device.endpoint).contains(query, Qt::CaseInsensitive) &&
                !text(device.tailscale_endpoint).contains(query, Qt::CaseInsensitive))
                continue;
            ++visible;
            auto* card = new QFrame(list);
            card->setObjectName("deviceCard");
            auto* contents = new QVBoxLayout(card);
            contents->setContentsMargins(18, 16, 18, 16);
            auto* top = new QHBoxLayout;
            auto* title = label(text(device.name), card);
            auto title_font = title->font();
            title_font.setBold(true);
            title_font.setPointSize(16);
            title->setFont(title_font);
            top->addWidget(title);
            top->addStretch();
            auto* edit = new QPushButton("Edit", card);
            top->addWidget(edit);
            contents->addLayout(top);
            contents->addWidget(label(
                device.endpoint.empty() ? "No direct address saved" : text(device.endpoint), card));
            if (!device.tailscale_endpoint.empty())
                contents->addWidget(label("Tailscale · " + text(device.tailscale_endpoint), card));
            auto* check_status = label("Connection not checked", card);
            contents->addWidget(check_status);
            auto* actions = new QHBoxLayout;
            auto* check = new QPushButton("Check connection", card);
            actions->addWidget(check);
            actions->addStretch();
            auto* unpair = new QPushButton("Unpair device…", card);
            unpair->setObjectName("unpairDevice-" + text(device.name));
            actions->addWidget(unpair);
            contents->addLayout(actions);
            cards->addWidget(card);
            QObject::connect(check, &QPushButton::clicked, &dialog, [&, device, check_status] {
                attempt([&] {
                    service.check_peer(device.name);
                    connectivity->setText("Checking " + text(device.name) + "…");
                    checked_label = check_status;
                });
            });
            QObject::connect(edit, &QPushButton::clicked, &dialog, [&, device] {
                attempt([&] {
                    const auto saved = edit_device(&dialog, service, device, changed);
                    changed();
                    render();
                    if (saved)
                        status->setText("Saved device changes.");
                });
            });
            QObject::connect(unpair, &QPushButton::clicked, &dialog, [&, device] {
                if (QMessageBox::question(&dialog, "Unpair " + text(device.name) + "?",
                                          "Active transfers and checks will stop. This removes "
                                          "local trust. Pair again to reconnect.",
                                          QMessageBox::Yes | QMessageBox::Cancel,
                                          QMessageBox::Cancel) != QMessageBox::Yes)
                    return;
                attempt([&] {
                    service.unpair(device.name);
                    changed();
                    render();
                    status->setText("Device unpaired.");
                });
            });
        }
        if (!visible)
            cards->addWidget(label(
                devices.empty() ? "Make your first connection. Add a device or restore a backup."
                                : "No matching devices. Try another name or address.",
                list));
        cards->addStretch();
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [&] { attempt(render); });
    QObject::connect(refresh, &QPushButton::clicked, &dialog, [&] {
        attempt([&] {
            changed();
            render();
            status->setText("Device list refreshed.");
        });
    });
#ifdef BEAM_HAS_PAIRING
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        attempt([&] {
            show_pairing_dialog(&dialog, config, service);
            changed();
            render();
        });
    });
#endif
    QObject::connect(identity, &QPushButton::clicked, &dialog, [&] {
        attempt([&] {
            const auto value = IdentityStore(config.state_directory).identity();
            QDialog info(&dialog);
            info.setWindowTitle("This device’s identity");
            auto* body = new QVBoxLayout(&info);
            body->addWidget(
                label("Compare all 64 SHA-256 characters when manually pairing.", &info));
            body->addWidget(label(text(value.fingerprint), &info));
            auto* pem = new QPlainTextEdit(text(value.certificate_pem), &info);
            pem->setReadOnly(true);
            body->addWidget(pem);
            auto* copy = new QPushButton("Copy public certificate", &info);
            body->addWidget(copy);
            QObject::connect(copy, &QPushButton::clicked, &info, [&] {
                QApplication::clipboard()->setText(text(value.certificate_pem));
            });
            info.resize(540, 400);
            info.exec();
        });
    });
    QObject::connect(export_button, &QPushButton::clicked, &dialog, [&] {
        attempt([&] {
            const auto path = QFileDialog::getSaveFileName(&dialog, "Export paired devices",
                                                           "beam-devices.beam-backup");
            if (path.isEmpty())
                return;
            write_device_backup(bytes(path),
                                IdentityStore(config.state_directory).export_devices());
            status->setText("Device backup saved (public certificates only).");
        });
    });
    QObject::connect(import_button, &QPushButton::clicked, &dialog, [&] {
        attempt([&] {
            const auto path = QFileDialog::getOpenFileName(&dialog, "Import device backup");
            if (path.isEmpty())
                return;
            const auto data = read_device_backup(bytes(path));
            const auto preview = IdentityStore(config.state_directory).preview_import(data);
            QDialog review(&dialog);
            review.setWindowTitle("Restore your devices");
            review.resize(600, 530);
            auto* body = new QVBoxLayout(&review);
            body->addWidget(label(QString::number(preview.added) + " to add · " +
                                      QString::number(preview.existing) + " already paired",
                                  &review));
            body->addWidget(label(
                "Only restore a backup you trust. Existing devices keep their current settings.",
                &review));
            if (preview.source_fingerprint !=
                IdentityStore(config.state_directory).identity().fingerprint)
                body->addWidget(label("This backup came from another identity. Pair this device on "
                                      "the other devices before transferring.",
                                      &review));
            QString details;
            for (const auto& peer : preview.peers)
                details += text(peer.name) + "\n" + text(peer.endpoint) +
                           "\nTailscale: " + text(peer.tailscale_endpoint) +
                           "\nSHA-256: " + text(peer.identity.fingerprint) + "\n\n";
            auto* content = new QPlainTextEdit(details, &review);
            content->setReadOnly(true);
            body->addWidget(content);
            auto* error = label({}, &review);
            body->addWidget(error);
            auto* buttons =
                new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &review);
            buttons->button(QDialogButtonBox::Ok)->setText("Restore devices");
            body->addWidget(buttons);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &review, [&] {
                try {
                    service.import_devices(data);
                    changed();
                    review.accept();
                } catch (const std::exception& failure) {
                    changed();
                    error->setText(text(failure.what()));
                }
            });
            if (review.exec() == QDialog::Accepted) {
                render();
                status->setText("Device backup restored.");
            }
        });
    });
    attempt(render);
    dialog.exec();
}
} // namespace beam::desktop
