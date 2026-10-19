#include "beam/identity.hpp"
#include "desktop/qt/window.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <chrono>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::logic_error(message);
}
template <typename Action> void rejects(Action action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::logic_error("invalid drop accepted");
}
template <typename Condition> void await(Condition condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do {
        QApplication::processEvents();
        if (condition())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("desktop UI state timed out");
}
std::uint16_t reserve_port() {
    const auto descriptor = socket(AF_INET, SOCK_DGRAM, 0);
    require(descriptor >= 0, "cannot create UI test socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const auto bound = bind(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    socklen_t length = sizeof(address);
    const auto result = getsockname(descriptor, reinterpret_cast<sockaddr*>(&address), &length);
    close(descriptor);
    require(bound == 0 && result == 0, "cannot reserve UI test port");
    return ntohs(address.sin_port);
}
void tests() {
    QTemporaryDir temporary;
    require(temporary.isValid(), "cannot create UI fixture");
    const auto root = std::filesystem::canonical(temporary.path().toUtf8().toStdString());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    std::ofstream(root / "hello.txt") << "test";
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "hello.txt").string()))});
    require(beam::desktop::dropped_file(mime) == root / "hello.txt", "local file drop rejected");
    mime.setUrls({QUrl("https://example.com/file")});
    rejects([&] { static_cast<void>(beam::desktop::dropped_file(mime)); });
    mime.setUrls({QUrl("file://server/share/file.txt")});
    rejects([&] { static_cast<void>(beam::desktop::dropped_file(mime)); });
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "hello.txt").string()) +
                                      QChar(0) + "/innocent.txt")});
    rejects([&] { static_cast<void>(beam::desktop::dropped_file(mime)); });
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString(root.string()))});
    rejects([&] { static_cast<void>(beam::desktop::dropped_file(mime)); });
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "hello.txt").string())),
                  QUrl::fromLocalFile("/etc/passwd")});
    rejects([&] { static_cast<void>(beam::desktop::dropped_file(mime)); });
    std::filesystem::create_symlink(root / "hello.txt", root / "link.txt");
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "link.txt").string()))});
    rejects([&] { static_cast<void>(beam::desktop::dropped_file(mime)); });

    auto* clipboard = QApplication::clipboard();
    clipboard->setText("private clipboard\r\n世界");
    beam::desktop::Window window({root / "missing-state", 4269, false});
    require(!window.windowIcon().pixmap(32, 32).isNull(), "packaged app icon cannot be loaded");
    auto* content = window.findChild<QPlainTextEdit*>("content");
    auto* load = window.findChild<QPushButton*>("loadClipboard");
    auto* send = window.findChild<QPushButton*>("send");
    auto* kind = window.findChild<QComboBox*>("kind");
    require(content && load && send && kind, "missing desktop controls");
    require(!window.findChild<QPushButton*>("receive"), "receive toggle is still present");
    require(window.findChild<QLineEdit*>("output")->text() == QDir::home().filePath("Downloads"),
            "unset receive folder did not default to Downloads");
    require(content->toPlainText().isEmpty(), "startup read the clipboard");
    require(!send->isEnabled(), "unpaired send enabled");
    require(window.findChild<QPushButton*>("manageDevices") != nullptr,
            "device management is missing");
    QTimer::singleShot(0, &window, [&] {
        auto* manager = window.findChild<QDialog*>("devicesDialog");
        require(manager && manager->findChild<QPushButton*>("importDevices"),
                "first-use device management cannot import a backup");
        manager->accept();
    });
    window.findChild<QPushButton*>("manageDevices")->click();
    load->click();
    require(content->toPlainText().contains("private clipboard") && kind->currentIndex() == 1,
            "explicit clipboard load failed");
    require(clipboard->text() == "private clipboard\r\n世界", "desktop wrote the clipboard");
    clipboard->setText("later clipboard");
    QApplication::processEvents();
    require(!content->toPlainText().contains("later"), "clipboard was watched after snapshot");
    clipboard->setText(QString(static_cast<qsizetype>(beam::max_text_size + 1), 'x'));
    load->click();
    require(!content->toPlainText().startsWith('x'),
            "oversized clipboard replaced valid selection");
    auto* status = window.findChild<QLabel*>("sendStatus");
    require(status && status->textFormat() == Qt::PlainText,
            "status interprets remote text as markup");
    require(status->text().contains("exceeds"), "clipboard validation error missing");
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "hello.txt").string()))});
    QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    // Drop dispatch requires an accepted drag-enter first; test that complete path.
    QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QApplication::sendEvent(&window, &drop);
    require(enter.isAccepted() && drop.isAccepted(), "window did not accept valid file drop");
    require(kind->currentIndex() == 0 &&
                window.findChild<QLineEdit*>("file")->text().endsWith("hello.txt"),
            "drop did not select file");
    require(!send->isEnabled(), "drop automatically enabled unpaired send");
    QDragEnterEvent move_only(QPoint(20, 20), Qt::MoveAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
    QApplication::sendEvent(&window, &move_only);
    require(!move_only.isAccepted(), "move-only drag could delete the source file");
    QDropEvent move_drop(QPointF(20, 20), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &move_drop);
    require(!move_drop.isAccepted(), "move-only drop was accepted");
    QDragEnterEvent mixed(QPoint(20, 20), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                          Qt::ShiftModifier);
    QApplication::sendEvent(&window, &mixed);
    QDropEvent mixed_drop(QPointF(20, 20), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                          Qt::ShiftModifier);
    QApplication::sendEvent(&window, &mixed_drop);
    require(mixed.isAccepted() && mixed.dropAction() == Qt::CopyAction && mixed_drop.isAccepted() &&
                mixed_drop.dropAction() == Qt::CopyAction,
            "mixed drag did not negotiate a copy");
    kind->setCurrentIndex(1);
    content->setPlainText("reviewed text");
    window.findChild<QLineEdit*>("file")->clear();
    QApplication::processEvents();
    QDragEnterEvent editor_enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton,
                                 Qt::NoModifier);
    QApplication::sendEvent(content->viewport(), &editor_enter);
    QDropEvent editor_drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(content->viewport(), &editor_drop);
    require(content->toPlainText() == "reviewed text" && kind->currentIndex() == 0 &&
                window.findChild<QLineEdit*>("file")->text().endsWith("hello.txt"),
            "editor drop bypassed the window's file selection");
    kind->setCurrentIndex(1);
    mime.setUrls({QUrl("https://example.com/one"), QUrl("https://example.com/two")});
    QDragEnterEvent remote_enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton,
                                 Qt::NoModifier);
    QApplication::sendEvent(content->viewport(), &remote_enter);
    require(!remote_enter.isAccepted() && content->toPlainText() == "reviewed text",
            "editor accepted remote URLs as a file drop");

    const auto left = root / "left", right = root / "right", output = root / "received";
    std::filesystem::create_directory(output);
    const auto left_identity = beam::IdentityStore(left, true).initialize();
    const auto right_identity = beam::IdentityStore(right, true).initialize();
    std::ofstream(root / "left.pem") << left_identity.certificate_pem;
    std::ofstream(root / "right.pem") << right_identity.certificate_pem;
    const auto port = reserve_port();
    beam::IdentityStore(left).pair("right", root / "right.pem", right_identity.fingerprint,
                                   "127.0.0.1:" + std::to_string(port));
    beam::IdentityStore(right).pair("left", root / "left.pem", left_identity.fingerprint);
    {
        beam::desktop::Window first({right, port, false});
        await([&] {
            return first.findChild<QLabel*>("receiveStatus")->text().startsWith("Listening");
        });
        require(first.findChild<QLineEdit*>("output")->text() == QDir::home().filePath("Downloads"),
                "first launch did not receive in Downloads");
    }
    {
        beam::desktop::Window automatic({right, port, false});
        auto* receive_status = automatic.findChild<QLabel*>("receiveStatus");
        await([&] { return receive_status->text().startsWith("Listening"); });
        automatic.select_receive_directory(output);
        await([&] { return receive_status->text().startsWith("Listening"); });
        beam::desktop::Service sender({left, port, false});
        sender.send({"right", beam::ContentKind::file, root / "hello.txt", {}});
        await([&] {
            return std::filesystem::exists(output / "hello.txt") && !sender.is_sending() &&
                   automatic.findChild<QLabel*>("lastResult")->text().contains("hello.txt") &&
                   receive_status->text().startsWith("Listening");
        });
        std::filesystem::remove(output / "hello.txt");
        clipboard->setText("unchanged clipboard");
        sender.send({"right", beam::ContentKind::text, {}, "automatic receive"});
        await([&] {
            return automatic.findChild<QPlainTextEdit*>("receivedText")->toPlainText() ==
                       "automatic receive" &&
                   !sender.is_sending();
        });
        require(std::filesystem::is_empty(output), "received text created a file");
        require(clipboard->text() == "unchanged clipboard",
                "receive wrote clipboard automatically");
        automatic.findChild<QPushButton*>("copyReceived")->click();
        require(clipboard->text() == "automatic receive", "explicit Copy lost received text");
        automatic.findChild<QPushButton*>("checkConnectivity")->click();
        await([&] {
            return automatic.findChild<QLabel*>("connectivityStatus")
                ->text()
                .contains("unavailable");
        });
        automatic.findChild<QPushButton*>("refreshPeers")->click();
        await([&] { return receive_status->text().startsWith("Listening"); });
    }
    QSettings("Beam", "Beam").setValue("receiveDirectory", QString::fromStdString(output.string()));
    {
        beam::desktop::Window restored({right, port, false});
        await([&] {
            return restored.findChild<QLabel*>("receiveStatus")->text().startsWith("Listening");
        });
        require(restored.findChild<QLineEdit*>("output")->text() ==
                    QString::fromStdString(output.string()),
                "launch lost remembered folder");
    }
    {
        beam::desktop::Window invalid({right, port, false}, root / "missing-output");
        await([&] {
            return invalid.findChild<QLabel*>("receiveStatus")
                ->text()
                .startsWith("Transfer stopped:");
        });
        require(invalid.findChild<QLineEdit*>("output")->text().endsWith("missing-output"),
                "explicit output lost to remembered folder");
        invalid.findChild<QPushButton*>("refreshPeers")->click();
        await([&] {
            return invalid.findChild<QLabel*>("receiveStatus")
                ->text()
                .startsWith("Transfer stopped:");
        });
        invalid.select_receive_directory(output);
        await([&] {
            return invalid.findChild<QLabel*>("receiveStatus")->text().startsWith("Listening");
        });
        const auto extra = beam::IdentityStore(root / "extra", true).initialize();
        std::ofstream(root / "extra.pem") << extra.certificate_pem;
        beam::IdentityStore(right).pair("spare", root / "extra.pem", extra.fingerprint);
        invalid.findChild<QPushButton*>("refreshPeers")->click();
        await([&] {
            return invalid.findChild<QLabel*>("receiveStatus")->text().startsWith("Listening");
        });
        auto* manage = invalid.findChild<QPushButton*>("manageDevices");
        auto* devices = invalid.findChild<QComboBox*>("peers");
        devices->setCurrentText("left");
        const auto confirm = [&](QMessageBox::StandardButton answer) {
            const auto selected = devices->currentText();
            QTimer::singleShot(0, &invalid, [&, answer, selected] {
                auto* manager = invalid.findChild<QDialog*>("devicesDialog");
                require(manager != nullptr, "device manager did not open");
                require(manager->findChild<QPushButton*>("exportDevices") &&
                            manager->findChild<QPushButton*>("importDevices"),
                        "backup actions missing");
                manager->findChild<QLineEdit*>("deviceSearch")->setText(selected);
                QTimer::singleShot(0, manager, [&, answer, manager] {
                    auto* dialog = manager->findChild<QMessageBox*>();
                    require(dialog != nullptr, "unpair skipped confirmation");
                    dialog->button(answer)->click();
                });
                auto* unpair = manager->findChild<QPushButton*>("unpairDevice-" + selected);
                require(unpair != nullptr, "device card lacks unpair");
                unpair->click();
                manager->accept();
            });
            manage->click();
        };
        confirm(QMessageBox::Cancel);
        require(beam::IdentityStore(right).peers().size() == 2, "cancelled unpair changed trust");
        confirm(QMessageBox::Yes);
        await([&] {
            return devices->count() == 1 && devices->currentText() == "spare" &&
                   invalid.findChild<QLabel*>("receiveStatus")->text().startsWith("Listening");
        });
        require(beam::IdentityStore(right).identity().fingerprint == right_identity.fingerprint &&
                    beam::IdentityStore(left).peer("right").identity.fingerprint ==
                        right_identity.fingerprint,
                "unpair changed identity or remote trust");
        confirm(QMessageBox::Yes);
        await([&] {
            return invalid.findChild<QLabel*>("receiveStatus")->text().contains("pair a device");
        });
        require(devices->count() == 0 && !invalid.findChild<QPushButton*>("send")->isEnabled(),
                "last-device unpair left stale controls");
    }
}
} // namespace
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        tests();
        std::cout << "desktop UI tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
