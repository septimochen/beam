#include "beam/identity.hpp"
#include "desktop/qt/window.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QMimeData>
#include <QPalette>
#include <QPixmap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <source_location>
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
template <typename Condition>
void await(Condition condition,
           const std::source_location& location = std::source_location::current()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do {
        QApplication::processEvents();
        if (condition())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("desktop UI state timed out at line " +
                             std::to_string(location.line()));
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
QQuickItem* visual_item(QQuickItem* root, const QString& name) {
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = visual_item(child, name))
            return found;
    return nullptr;
}
void tests() {
    QTemporaryDir temporary;
    require(temporary.isValid(), "cannot create UI fixture");
    const auto root = std::filesystem::canonical(temporary.path().toUtf8().toStdString());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, temporary.path());
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
    require(!window.view()->icon().pixmap(32, 32).isNull(), "packaged icon cannot be loaded");
    auto* content = window.view()->findChild<QQuickItem*>("content");
    auto* load = window.view()->findChild<QQuickItem*>("loadClipboard");
    auto* send = window.view()->findChild<QQuickItem*>("send");
    require(content && load && send, "missing QML controls");
    const auto click = [](QObject* button) {
        require(QMetaObject::invokeMethod(button, "clicked"), "cannot invoke QML action");
    };
    require(window.state()["output"].toString() == QDir::home().filePath("Downloads"),
            "unset receive folder did not default to Downloads");
    require(window.content().isEmpty(), "startup read clipboard");
    require(!send->isEnabled(), "unpaired send enabled");
    click(window.view()->findChild<QQuickItem*>("manageDevices"));
    require(window.state()["page"].toInt() == 1 &&
                window.view()->findChild<QQuickItem*>("importDevices"),
            "first-use device management cannot import backup");
    window.show_share();
    click(load);
    require(window.content().contains("private clipboard") && window.kind() == 1 &&
                content->property("text").toString() == window.content(),
            "explicit clipboard load did not update QML editor");
    require(clipboard->text() == "private clipboard\r\n世界", "desktop wrote clipboard");
    clipboard->setText("later clipboard");
    QApplication::processEvents();
    require(!window.content().contains("later"), "clipboard watched after snapshot");
    clipboard->setText(QString(static_cast<qsizetype>(beam::max_text_size + 1), 'x'));
    click(load);
    require(!window.content().startsWith('x') &&
                window.state()["sendStatus"].toString().contains("exceeds"),
            "oversized clipboard replaced selection or lost error");
    clipboard->setText(QString(32769, QChar(0x754c)));
    click(load);
    require(!window.content().startsWith(QChar(0x754c)), "UTF-8 byte limit bypassed");
    window.show();
    QApplication::processEvents();
    content->forceActiveFocus();
    content->setProperty("text", "reviewed text");
    require(window.content() == "reviewed text", "QML editor edits did not reach controller");
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "hello.txt").string()))});
    // Window filter runs before the editor, including drops over its text area.
    const auto drop_at_window = [&](Qt::DropActions actions, bool accepted) {
        QDragEnterEvent enter(QPoint(20, 20), actions, &mime, Qt::LeftButton, Qt::ShiftModifier);
        QApplication::sendEvent(window.view(), &enter);
        QDropEvent drop(QPointF(20, 20), actions, &mime, Qt::LeftButton, Qt::ShiftModifier);
        QApplication::sendEvent(window.view(), &drop);
        require(enter.isAccepted() == accepted && drop.isAccepted() == accepted,
                "incorrect drag/drop acceptance");
        if (accepted)
            require(enter.dropAction() == Qt::CopyAction && drop.dropAction() == Qt::CopyAction,
                    "file drop negotiated a move");
    };
    drop_at_window(Qt::CopyAction, true);
    require(window.kind() == 0 && window.state()["file"].toString().endsWith("hello.txt") &&
                window.content() == "reviewed text" && !send->isEnabled(),
            "drop changed reviewed text or enabled unpaired send");
    drop_at_window(Qt::MoveAction, false);
    drop_at_window(Qt::CopyAction | Qt::MoveAction, true);
    mime.setUrls({QUrl("https://example.com/one"), QUrl("https://example.com/two")});
    drop_at_window(Qt::CopyAction, false);
    require(window.content() == "reviewed text", "remote URLs entered editor");

    window.show_transfers();
    require(window.transfer_panel()->isVisible(), "transfer panel did not open");
    window.transfer_panel()->close();
    window.show_transfers();
    require(window.transfer_panel()->isVisible(), "transfer panel did not reopen");
    window.transfer_panel()->hide();
    if (qEnvironmentVariableIsSet("BEAM_UI_SCREENSHOT")) {
        await([&] { return window.view()->isExposed(); });
        require(window.view()->grabWindow().save(qEnvironmentVariable("BEAM_UI_SCREENSHOT")),
                "cannot save UI preview");
    }

    const auto saved_palette = QApplication::palette();
    auto dark_palette = saved_palette;
    dark_palette.setColor(QPalette::Window, QColor("#17191f"));
    dark_palette.setColor(QPalette::Base, QColor("#22252d"));
    dark_palette.setColor(QPalette::Text, QColor("#f1f3f8"));
    dark_palette.setColor(QPalette::WindowText, QColor("#f1f3f8"));
    dark_palette.setColor(QPalette::PlaceholderText, QColor("#b3bac8"));
    QApplication::setPalette(dark_palette);
    await([&] { return window.view()->color() == QColor("#17191f"); });
    if (qEnvironmentVariableIsSet("BEAM_UI_SCREENSHOT"))
        require(window.view()->grabWindow().save(qEnvironmentVariable("BEAM_UI_SCREENSHOT") +
                                                 ".dark.png"),
                "cannot save dark UI preview");
    QApplication::setPalette(saved_palette);

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
        await([&] { return first.state()["receiveStatus"].toString().startsWith("Listening"); });
        require(first.state()["output"].toString() == QDir::home().filePath("Downloads"),
                "first launch did not receive in Downloads");
    }
    {
        beam::desktop::Window automatic({right, port, false});
        await(
            [&] { return automatic.state()["receiveStatus"].toString().startsWith("Listening"); });
        automatic.select_receive_directory(output);
        await(
            [&] { return automatic.state()["receiveStatus"].toString().startsWith("Listening"); });
        beam::desktop::Window sender({left, reserve_port(), false}, output);
        QMimeData file;
        file.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "hello.txt").string()))});
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &file, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(sender.view(), &drop);
        click(sender.view()->findChild<QQuickItem*>("send"));
        await([&] {
            return std::filesystem::exists(output / "hello.txt") &&
                   !sender.state()["sending"].toBool() &&
                   automatic.state()["lastResult"].toString().contains("hello.txt") &&
                   sender.state()["sendStatus"].toString().startsWith("Sent and verified") &&
                   automatic.state()["receiveStatus"].toString().startsWith("Listening");
        });
        require(sender.state()["sendProgress"].toDouble() == 1.0 &&
                    !sender.state()["sendBusy"].toBool(),
                "verified send did not complete progress");
        sender.show_transfers();
        require(sender.transfer_panel()->isVisible(), "completed transfers panel failed");
        const auto* panel_send = sender.transfer_panel()->findChild<QObject*>("panelSend");
        require(panel_send && panel_send->property("progress").toDouble() == 1.0 &&
                    panel_send->property("status").toString() ==
                        sender.state()["sendStatus"].toString(),
                "transfer panel did not share verified progress");
        const auto actions = sender.findChild<QSystemTrayIcon*>()->contextMenu()->actions();
        require(std::any_of(actions.begin(), actions.end(),
                            [](const QAction* action) {
                                return action->text().startsWith("Send: Sent and verified");
                            }),
                "standard tray menu lost verified result");
        std::filesystem::remove(output / "hello.txt");
        // Unedited snapshots retain CRLF and supplementary Unicode through QML and QUIC.
        const QString original = "exact clipboard\r\n世界 😀";
        clipboard->setText(original);
        sender.load_clipboard();
        clipboard->setText("unchanged clipboard");
        click(sender.view()->findChild<QQuickItem*>("send"));
        await([&] {
            return automatic.state()["receivedText"].toString() == original &&
                   automatic.state()["receiveStatus"].toString().startsWith("Listening") &&
                   !sender.state()["sending"].toBool();
        });
        require(std::filesystem::is_empty(output), "received text created file");
        require(clipboard->text() == "unchanged clipboard", "receive wrote clipboard");
        click(automatic.view()->findChild<QQuickItem*>("copyReceived"));
        require(clipboard->text() == original, "explicit Copy lost received text");
        sender.set_content("<b>plain text</b>");
        sender.send_selection();
        await([&] {
            return automatic.state()["receivedText"].toString() == "<b>plain text</b>" &&
                   !sender.state()["sending"].toBool();
        });
        auto* received = automatic.view()->findChild<QQuickItem*>("receivedText");
        require(received->property("textFormat").toInt() == 0 &&
                    received->property("text").toString() == "<b>plain text</b>",
                "received text interpreted markup");
        automatic.check_peer("left");
        await([&] { return !automatic.state()["checking"].toBool(); });
        automatic.refresh_peers();
        await(
            [&] { return automatic.state()["receiveStatus"].toString().startsWith("Listening"); });
        if (qEnvironmentVariableIsSet("BEAM_UI_SCREENSHOT")) {
            automatic.open_devices();
            automatic.show();
            await([&] { return automatic.view()->isExposed(); });
            require(automatic.view()->grabWindow().save(qEnvironmentVariable("BEAM_UI_SCREENSHOT") +
                                                        ".devices.png"),
                    "cannot save devices preview");
            automatic.show_transfers();
            await([&] { return automatic.transfer_panel()->isExposed(); });
            require(automatic.transfer_panel()->grabWindow().save(
                        qEnvironmentVariable("BEAM_UI_SCREENSHOT") + ".transfers.png"),
                    "cannot save panel preview");
        }
    }
    {
        beam::desktop::Window restored({right, port, false});
        await([&] { return restored.state()["receiveStatus"].toString().startsWith("Listening"); });
        require(restored.state()["output"].toString() == QString::fromStdString(output.string()),
                "launch lost remembered folder");
    }
    {
        beam::desktop::Window invalid({right, port, false}, root / "missing-output");
        await([&] {
            return invalid.state()["receiveStatus"].toString().startsWith("Transfer stopped:");
        });
        require(invalid.state()["output"].toString().endsWith("missing-output"),
                "explicit output lost precedence");
        invalid.refresh_peers();
        await([&] {
            return invalid.state()["receiveStatus"].toString().startsWith("Transfer stopped:");
        });
        invalid.select_receive_directory(output);
        await([&] { return invalid.state()["receiveStatus"].toString().startsWith("Listening"); });
        invalid.begin_edit("left");
        require(!invalid.save_device({{"name", "Invalid name"}}) &&
                    !invalid.state()["deviceError"].toString().isEmpty(),
                "invalid edit accepted or error missing");
        require(invalid.save_device(
                    {{"name", "renamed"}, {"endpoint", "127.0.0.1:4269"}, {"fallback", ""}}),
                "valid edit failed");
        require(beam::IdentityStore(right).peer("renamed").identity.fingerprint ==
                    left_identity.fingerprint,
                "rename changed trust");
        require(!invalid.restore_devices(), "restore skipped preview");
        invalid.show_identity();
        require(invalid.state()["identity"].toMap()["fingerprint"].toString() ==
                    QString::fromStdString(right_identity.fingerprint),
                "identity view exposes wrong certificate");
        invalid.open_devices();
        invalid.show();
        await([&] {
            return visual_item(invalid.view()->contentItem(), "unpairDevice-renamed") != nullptr;
        });
        auto* remove = visual_item(invalid.view()->contentItem(), "unpairDevice-renamed");
        auto* confirmation = invalid.view()->findChild<QObject*>("unpairConfirmation");
        require(confirmation != nullptr, "QML unpair confirmation missing");
        click(remove);
        require(confirmation->property("visible").toBool() &&
                    beam::IdentityStore(right).peers().size() == 1,
                "unpair skipped confirmation");
        require(QMetaObject::invokeMethod(confirmation, "reject"), "cannot cancel unpair dialog");
        require(beam::IdentityStore(right).peers().size() == 1, "cancelled unpair changed trust");
        click(remove);
        click(invalid.view()->findChild<QQuickItem*>("confirmUnpair"));
        await(
            [&] { return invalid.state()["receiveStatus"].toString().contains("pair a device"); });
        require(invalid.state()["peers"].toStringList().isEmpty() &&
                    !invalid.view()->findChild<QQuickItem*>("send")->isEnabled(),
                "last-peer unpair left stale controls");
        require(beam::IdentityStore(right).identity().fingerprint == right_identity.fingerprint &&
                    beam::IdentityStore(left).peer("right").identity.fingerprint ==
                        right_identity.fingerprint,
                "unpair changed identity or remote trust");
#ifdef BEAM_HAS_PAIRING
        invalid.open_pairing();
        // Hosts without a multicast-capable interface can report a visible startup error.
        if (invalid.state()["pairingOpen"].toBool()) {
            invalid.pairing_action("join", "invalid invitation");
            require(!invalid.state()["deviceError"].toString().isEmpty(),
                    "invalid invitation error lost");
            invalid.close_pairing();
            require(!invalid.state()["pairingOpen"].toBool() &&
                        invalid.state()["pairing"].toMap().isEmpty(),
                    "pairing close retained invitation");
        }
#endif
    }
#ifdef BEAM_HAS_PAIRING
    {
        const auto partner_state = root / "pairing-partner";
        const auto ui_state = root / "pairing-ui";
        beam::IdentityStore(partner_state, true).initialize();
        beam::IdentityStore(ui_state, true).initialize();
        beam::PairingOptions options{partner_state, "Pairing partner", reserve_port()};
        options.addresses = {"127.0.0.1"};
        beam::PairingSession partner(options);
        partner.host();
        beam::desktop::Window joining({ui_state, reserve_port(), false}, output);
        joining.show();
        joining.open_pairing();
        require(joining.state()["pairingOpen"].toBool(), "QML pairing dialog failed to host");
        joining.pairing_action("join", QString::fromStdString(partner.snapshot().invitation));
        await([&] {
            return joining.state()["pairing"].toMap()["state"].toString() == "awaiting approval" &&
                   partner.snapshot().state == beam::PairingState::approval;
        });
        auto* alias = joining.view()->findChild<QQuickItem*>("pairingAlias");
        auto* approve = joining.view()->findChild<QQuickItem*>("approveDevice");
        require(alias && approve && alias->isVisible() && approve->isVisible() &&
                    !alias->property("text").toString().isEmpty(),
                "pairing approval controls or suggested alias missing");
        partner.approve("qml");
        require(beam::IdentityStore(ui_state).peers().empty(),
                "pairing trusted peer before local approval");
        alias->setProperty("text", "partner");
        click(approve);
        await([&] {
            return joining.state()["pairing"].toMap()["state"].toString() == "paired" &&
                   partner.snapshot().state == beam::PairingState::paired;
        });
        require(beam::IdentityStore(ui_state).peer("partner").identity.fingerprint ==
                    beam::IdentityStore(partner_state).identity().fingerprint,
                "QML pairing stored wrong identity");
        require(QMetaObject::invokeMethod(joining.view()->findChild<QObject*>("pairingDialog"),
                                          "close"),
                "cannot close QML pairing dialog");
        await([&] { return !joining.state()["pairingOpen"].toBool(); });
    }
#endif
}
} // namespace
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        tests();
        std::cout << "desktop QML UI tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
