#include "pairing_dialog.hpp"
#include "beam/pairing.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

namespace beam::desktop {
void show_pairing_dialog(QWidget* parent, const Config& config, Service& service) {
    if (service.is_sending() || service.is_checking())
        throw std::logic_error("Wait for the active transfer before pairing");
    PairingSession session(
        {config.state_directory, QSysInfo::machineHostName().toUtf8().toStdString(), config.port});
    QDialog dialog(parent);
    dialog.setWindowTitle("Add device");
    dialog.resize(540, 720);
    auto* outer = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    auto* content = new QWidget(scroll);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    auto* help = new QLabel("Open Add device on both devices on the same local network.", &dialog);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* status = new QLabel(&dialog);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* error_status = new QLabel(&dialog);
    error_status->setTextFormat(Qt::PlainText);
    error_status->setWordWrap(true);
    layout->addWidget(error_status);
    auto* pin = new QLabel(&dialog);
    pin->setTextFormat(Qt::PlainText);
    layout->addWidget(pin);
    auto* qr = new QLabel(&dialog);
    qr->setAccessibleName("Pairing QR code");
    layout->addWidget(qr);
    auto* copy = new QPushButton("Copy invitation", &dialog);
    layout->addWidget(copy);
    auto* nearby = new QComboBox(&dialog);
    nearby->setAccessibleName("Nearby device");
    layout->addWidget(nearby);
    auto* browse = new QPushButton("Find nearby devices", &dialog);
    layout->addWidget(browse);
    auto* code = new QLineEdit(&dialog);
    code->setMaxLength(6);
    code->setPlaceholderText("Six-digit PIN shown on the other device");
    layout->addWidget(code);
    auto* pair = new QPushButton("Pair nearby", &dialog);
    layout->addWidget(pair);
    auto* invitation = new QLineEdit(&dialog);
    invitation->setMaxLength(2048);
    invitation->setPlaceholderText("Paste a Beam invitation");
    layout->addWidget(invitation);
    auto* join = new QPushButton("Use invitation", &dialog);
    layout->addWidget(join);
    auto* alias = new QLineEdit(&dialog);
    alias->setMaxLength(63);
    alias->setPlaceholderText("Local alias for this device");
    layout->addWidget(alias);
    auto* approve = new QPushButton("Approve device", &dialog);
    layout->addWidget(approve);
    auto* decline = new QPushButton("Decline", &dialog);
    layout->addWidget(decline);
    auto* again = new QPushButton("New invitation", &dialog);
    layout->addWidget(again);
    auto* forget = new QPushButton("Forget pending approval", &dialog);
    layout->addWidget(forget);
    auto* close = new QPushButton("Done", &dialog);
    layout->addWidget(close);
    const auto attempt = [error_status](const std::function<void()>& action) {
        try {
            error_status->clear();
            action();
        } catch (const std::exception& error) {
            error_status->setText(QString::fromUtf8(error.what()));
        }
    };
    QObject::connect(copy, &QPushButton::clicked, &dialog, [&] {
        QApplication::clipboard()->setText(QString::fromStdString(session.snapshot().invitation));
    });
    QObject::connect(browse, &QPushButton::clicked, &dialog,
                     [&] { attempt([&] { session.browse(); }); });
    QObject::connect(pair, &QPushButton::clicked, &dialog, [&] {
        attempt([&] {
            session.join_pin(nearby->currentData().toString().toStdString(),
                             code->text().toStdString());
        });
    });
    QObject::connect(join, &QPushButton::clicked, &dialog, [&] {
        attempt([&] { session.join(invitation->text().trimmed().toStdString()); });
    });
    QObject::connect(approve, &QPushButton::clicked, &dialog, [&] {
        attempt([&] {
            service.stop_receiving();
            session.approve(alias->text().toStdString());
        });
    });
    QObject::connect(again, &QPushButton::clicked, &dialog,
                     [&] { attempt([&] { session.host(); }); });
    QObject::connect(forget, &QPushButton::clicked, &dialog,
                     [&] { attempt([&] { session.forget_incomplete(); }); });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(decline, &QPushButton::clicked, &dialog,
                     [&] { attempt([&] { session.reject(); }); });
    PairingState previous = PairingState::idle;
    std::string last_invitation;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
        const auto snapshot = session.snapshot();
        status->setText(QString::fromStdString(std::string(pairing_state_name(snapshot.state)) +
                                               ": " + snapshot.message));
        pin->setText(QString::fromStdString("PIN: " + snapshot.pin));
        const bool hosting = snapshot.state == PairingState::hosting;
        pin->setVisible(hosting);
        copy->setEnabled(hosting);
        qr->setVisible(hosting);
        approve->setVisible(snapshot.state == PairingState::approval);
        decline->setVisible(snapshot.state == PairingState::approval);
        alias->setVisible(snapshot.state == PairingState::approval);
        forget->setVisible(snapshot.state == PairingState::incomplete);
        if (snapshot.state != previous) {
            previous = snapshot.state;
            if (snapshot.state == PairingState::approval)
                alias->setText(QString::fromStdString(snapshot.alias));
        }
        if (snapshot.invitation != last_invitation) {
            last_invitation = snapshot.invitation;
            if (!snapshot.qr.empty()) {
                const int size = static_cast<int>(snapshot.qr.size());
                QImage image(size + 8, size + 8, QImage::Format_RGB32);
                image.fill(Qt::white);
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x)
                        if (snapshot.qr[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] ==
                            '1')
                            image.setPixelColor(x + 4, y + 4, Qt::black);
                qr->setPixmap(QPixmap::fromImage(image).scaled(280, 280, Qt::KeepAspectRatio,
                                                               Qt::FastTransformation));
            }
        }
        if (nearby->count() != static_cast<int>(snapshot.nearby.size())) {
            const auto selected = nearby->currentData();
            nearby->clear();
            for (const auto& device : snapshot.nearby)
                nearby->addItem(QString::fromStdString(device.name),
                                QString::fromStdString(device.endpoint));
            const auto found = nearby->findData(selected);
            if (found >= 0)
                nearby->setCurrentIndex(found);
        }
    });
    session.host();
    session.browse();
    timer.start(100);
    dialog.exec();
    session.cancel();
}
} // namespace beam::desktop
