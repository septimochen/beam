#include "window.hpp"
#include "beam/identity.hpp"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QIcon>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QSysInfo>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <array>

void qml_register_types_Beam();

namespace beam::desktop {
namespace {
QString display(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
QString human_size(long double bytes) {
    constexpr std::array units{"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    std::size_t unit = 0;
    while (bytes >= 1000 && unit + 1 < units.size()) {
        bytes /= 1000;
        ++unit;
    }
    return QLocale::c().toString(static_cast<double>(bytes), 'f', unit == 0 ? 0 : 2) + " " +
           units[unit];
}
std::string bytes(const QString& value) { return value.toUtf8().toStdString(); }
QIcon icon() { return QIcon(":/beam/icons/beam.png"); }
QString stage_name(TransferStage stage) {
    switch (stage) {
    case TransferStage::hashing:
        return "Hashing";
    case TransferStage::connecting:
        return "Connecting";
    case TransferStage::waiting:
        return "Waiting";
    case TransferStage::transferring:
        return "Transferring";
    case TransferStage::verifying:
        return "Verifying";
    case TransferStage::complete:
        return "Verified";
    }
    return {};
}
} // namespace
std::filesystem::path dropped_file(const QMimeData& mime) {
    const auto urls = mime.urls();
    if (urls.size() != 1 || !urls.front().isLocalFile() || !urls.front().host().isEmpty())
        throw std::invalid_argument("Drop one local file at a time");
    const std::filesystem::path path(bytes(urls.front().toLocalFile()));
    validate_request({"selection", ContentKind::file, path, {}});
    return path;
}
Window::Window(Config configuration, const std::filesystem::path& receive_directory)
    : config(std::move(configuration)), service(config), menu(std::make_unique<QMenu>()),
      engine(std::make_unique<QQmlApplicationEngine>()) {
    static const bool registered = [] {
        qml_register_types_Beam();
        return true;
    }();
    Q_UNUSED(registered)
    QApplication::setQuitOnLastWindowClosed(false);
    auto output = receive_directory.empty()
                      ? QSettings("Beam", "Beam").value("receiveDirectory").toString()
                      : display(receive_directory.string());
    use_default_directory = output.isEmpty();
    if (use_default_directory)
        output = QDir::home().filePath("Downloads");
    values = {{"output", output},
              {"file", ""},
              {"receivedText", ""},
              {"hasReceivedText", false},
              {"sendStatus", "Choose a paired device and content."},
              {"receiveStatus", "Stopped."},
              {"lastResult", "No completed transfers yet."},
              {"sendProgress", 0.0},
              {"receiveProgress", 0.0},
              {"sendBusy", false},
              {"receiveBusy", false},
              {"deviceError", ""},
              {"deviceStatus", ""},
              {"connectivityStatus", ""},
              {"page", 0},
              {"pairingOpen", false}};
#ifdef BEAM_HAS_PAIRING
    values["pairingSupported"] = true;
#else
    values["pairingSupported"] = false;
#endif
    tray = new QSystemTrayIcon(icon(), this);
    tray->setToolTip("Beam");
    tray_send = menu->addAction("Send: idle");
    tray_receive = menu->addAction("Receive: idle");
    tray_send->setEnabled(false);
    tray_receive->setEnabled(false);
    menu->addSeparator();
    connect(menu->addAction("Show transfers…"), &QAction::triggered, this, &Window::show_transfers);
    connect(menu->addAction("Open Beam"), &QAction::triggered, this, &Window::show_window);
    connect(menu->addAction("Load clipboard text…"), &QAction::triggered, this, [this] {
        show_window();
        values["page"] = 0;
        load_clipboard();
    });
    menu->addSeparator();
    connect(menu->addAction("Quit Beam"), &QAction::triggered, qApp, &QApplication::quit);
    tray->setContextMenu(menu.get());
    connect(tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    show_window();
            });
    connect(this, &Window::changed, this, [this] { last_snapshot = state(); });
    engine->setInitialProperties({{"model", QVariant::fromValue(this)}});
    engine->load(QUrl("qrc:/beam/Beam/Main.qml"));
    if (engine->rootObjects().isEmpty())
        throw std::runtime_error("Cannot load the Beam QML interface");
    main_window = qobject_cast<QQuickWindow*>(engine->rootObjects().front());
    if (!main_window)
        throw std::runtime_error("Beam QML root is not a window");
    panel = main_window->findChild<QQuickWindow*>("transferPanel");
    if (!panel)
        throw std::runtime_error("Cannot load the transfer panel");
    main_window->setIcon(icon());
    panel->setIcon(icon());
    main_window->installEventFilter(this);
    tray->show();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Window::poll);
    timer->start(100);
    refresh_peers();
}
Window::~Window() {
#ifdef BEAM_HAS_PAIRING
    pairing.reset();
#endif
    tray->hide();
    tray->setContextMenu(nullptr);
    engine.reset();
}
QVariantMap Window::state() const {
    auto result = values;
    result["sending"] = service.is_sending();
    result["checking"] = service.is_checking();
    result["deviceChecks"] = device_checks;
    return result;
}
void Window::set_content(const QString& value) {
    if (outgoing == value)
        return;
    outgoing = value;
    clipboard_snapshot.reset();
    emit changed();
}
void Window::set_peer(const QString& value) {
    if (selected_peer == value)
        return;
    selected_peer = value;
    emit changed();
}
void Window::set_kind(int value) {
    if (value != 0 && value != 1)
        return;
    selection = value;
    emit changed();
}
void Window::show_window() {
    main_window->showNormal();
    main_window->raise();
    main_window->requestActivate();
}
void Window::show_share() {
    values["page"] = 0;
    emit changed();
}
void Window::show_transfers() {
    panel->showNormal();
    panel->raise();
    panel->requestActivate();
}
void Window::hide() { main_window->hide(); }
void Window::showMinimized() { main_window->showMinimized(); }
bool Window::isVisible() const { return main_window->isVisible(); }
bool Window::isMinimized() const { return main_window->windowState() == Qt::WindowMinimized; }
void Window::attempt(const std::function<void()>& action) {
    values["deviceError"] = "";
    try {
        action();
    } catch (const std::exception& error) {
        values["deviceError"] = display(error.what());
    }
    emit changed();
}
void Window::refresh_peers() {
    QStringList names;
    QVariantList devices;
    try {
        for (const auto& peer : IdentityStore(config.state_directory).peers()) {
            names.append(display(peer.name));
            devices.append(QVariantMap{{"name", display(peer.name)},
                                       {"endpoint", display(peer.endpoint)},
                                       {"fallback", display(peer.fallback_endpoint)}});
        }
        if (names.isEmpty())
            values["sendStatus"] = "No paired devices. Choose Add device to pair.";
    } catch (const std::exception& error) {
        values["sendStatus"] = "Pairing setup needed: " + display(error.what());
    }
    values["peers"] = names;
    values["devices"] = devices;
    if (!names.contains(selected_peer))
        selected_peer = names.isEmpty() ? QString{} : names.front();
    restart_receiving();
    emit changed();
}
void Window::open_devices() {
    attempt([this] {
        IdentityStore(config.state_directory, true).initialize();
        values["page"] = 1;
        refresh_peers();
    });
}
void Window::start_receiving_when_ready() {
    if (!receive_start_pending || service.is_receiving())
        return;
    receive_start_pending = false;
    if (values["peers"].toStringList().isEmpty()) {
        values["receiveStatus"] = "Choose Add device to pair a device.";
        return;
    }
    try {
        const auto directory = bytes(values["output"].toString());
        if (use_default_directory)
            std::filesystem::create_directory(directory);
        service.start_receiving(directory);
        values["receiveStatus"] = "Starting…";
    } catch (const std::exception& error) {
        values["receiveStatus"] = "Transfer stopped: " + display(error.what());
    }
}
void Window::restart_receiving() {
    receive_start_pending = true;
    if (service.is_receiving()) {
        service.stop_receiving();
        values["receiveStatus"] = "Updating receive settings…";
    } else {
        start_receiving_when_ready();
    }
}
void Window::select_receive_directory(const std::filesystem::path& directory) {
    if (directory.empty())
        return;
    values["output"] = display(directory.string());
    QSettings("Beam", "Beam").setValue("receiveDirectory", values["output"]);
    use_default_directory = false;
    restart_receiving();
    emit changed();
}
void Window::choose_file() {
    const auto path = QFileDialog::getOpenFileName(nullptr, "Choose one file");
    if (path.isEmpty())
        return;
    try {
        validate_request({"selection", ContentKind::file, bytes(path), {}});
        values["file"] = path;
        selection = 0;
    } catch (const std::exception& error) {
        values["sendStatus"] = display(error.what());
    }
    emit changed();
}
void Window::choose_folder() {
    select_receive_directory(bytes(
        QFileDialog::getExistingDirectory(nullptr, "Receive folder", values["output"].toString())));
}
void Window::load_clipboard() {
    try {
        const auto* mime = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
        if (!mime || !mime->hasText())
            throw std::invalid_argument("Clipboard has no text; choose a file for other content");
        const auto snapshot = mime->text();
        if (snapshot.size() > static_cast<qsizetype>(max_text_size))
            throw std::invalid_argument("Clipboard text exceeds 64 KiB");
        const auto content = bytes(snapshot);
        validate_content(ContentKind::text, content);
        // The editor normalizes line endings; preserve the original until a user edit.
        outgoing = snapshot;
        outgoing.replace("\r\n", "\n").replace('\r', '\n');
        clipboard_snapshot = content;
        selection = 1;
        values["sendStatus"] = "Clipboard snapshot loaded. Review it, choose a device, then Send.";
    } catch (const std::exception& error) {
        values["sendStatus"] = display(error.what());
    }
    emit changed();
}
void Window::copy_text(bool received) {
    QApplication::clipboard()->setText(received ? values["receivedText"].toString() : outgoing);
}
void Window::send_selection() {
    try {
        if (values["pairingOpen"].toBool())
            throw std::logic_error("Close Add device before sending");
        SendRequest request{bytes(selected_peer),
                            selection == 0 ? ContentKind::file : ContentKind::text,
                            bytes(values["file"].toString()),
                            {}};
        if (selection == 1) {
            if (outgoing.size() > static_cast<qsizetype>(max_text_size))
                throw std::invalid_argument("Text exceeds 64 KiB");
            request.content = clipboard_snapshot.value_or(bytes(outgoing));
        }
        service.send(std::move(request));
        values["sendProgress"] = 0.0;
        values["sendBusy"] = true;
        values["sendStatus"] = "Sending…";
    } catch (const std::exception& error) {
        values["sendStatus"] = display(error.what());
    }
    emit changed();
}
void Window::cancel_send() { service.cancel_send(); }
void Window::check_peer(const QString& name) {
    attempt([&] {
        if (values["pairingOpen"].toBool())
            throw std::logic_error("Close Add device before checking connectivity");
        service.check_peer(bytes(name));
        checked_peer = name;
        values["connectivityStatus"] = "Checking…";
        device_checks[name] = "Checking…";
    });
}
void Window::poll() {
    for (const auto& event : service.take_events())
        report(event);
    start_receiving_when_ready();
    poll_pairing();
    tray_send->setText("Send: " + values["sendStatus"].toString());
    tray_receive->setText("Receive: " + values["receiveStatus"].toString());
    if (state() != last_snapshot)
        emit changed();
}
void Window::report(const Event& event) {
    if (event.direction == Direction::check) {
        values["connectivityStatus"] = display(event.message);
        device_checks[checked_peer] = display(event.message);
        return;
    }
    const bool send = event.direction == Direction::send;
    const auto status = send ? "sendStatus" : "receiveStatus";
    const auto progress = send ? "sendProgress" : "receiveProgress";
    const auto busy = send ? "sendBusy" : "receiveBusy";
    QString message;
    switch (event.kind) {
    case EventKind::progress: {
        const auto& update = event.progress;
        const bool counted = update.stage == TransferStage::transferring;
        values[busy] = !counted;
        const auto fraction = update.total == 0 ? 0.0
                                                : static_cast<double>(update.bytes) /
                                                      static_cast<double>(update.total);
        values[progress] = counted ? fraction : 0.0;
        auto label = stage_name(update.stage);
        if (counted)
            label += QString(" · %1% · %2 / %3 · %4/s")
                         .arg(static_cast<int>(fraction * 100))
                         .arg(human_size(update.bytes), human_size(update.total),
                              human_size(update.bytes_per_second));
        values[status] = label;
        return;
    }
    case EventKind::listening:
        values["receiveBusy"] = false;
        values["receiveProgress"] = 0.0;
        values[status] = QString("Listening for paired devices on port %1.").arg(config.port);
        return;
    case EventKind::completed:
        if (!send && event.result.kind == ContentKind::text) {
            values["receivedText"] = display(event.result.text);
            values["hasReceivedText"] = true;
        }
        values[progress] = 1.0;
        values[busy] = false;
        message = send ? "Sent and verified with " + display(event.message)
                       : "Received and verified " + display(event.result.filename);
        message += " (" + human_size(event.result.bytes) + ")";
        break;
    case EventKind::failed:
        values[progress] = 0.0;
        values[busy] = false;
        message = "Transfer stopped: " + display(event.message);
        break;
    case EventKind::warning:
        values[status] = "LAN advertisement unavailable: " + display(event.message);
        return;
    case EventKind::stopped:
        values[progress] = 0.0;
        values[busy] = false;
        if (values["peers"].toStringList().isEmpty())
            values[status] = "Choose Add device to pair a device.";
        else if (!values[status].toString().startsWith("Transfer stopped:"))
            values[status] = "Receiver stopped.";
        return;
    }
    values[status] = message;
    values["lastResult"] = message;
    if (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages())
        tray->showMessage("Beam", message,
                          event.kind == EventKind::failed ? QSystemTrayIcon::Warning
                                                          : QSystemTrayIcon::Information);
}
bool Window::eventFilter(QObject* watched, QEvent* event) {
    if (watched != main_window)
        return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::Close) {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            main_window->hide();
            event->ignore();
            return true;
        }
        panel->hide();
        QApplication::quit();
    }
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
        event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        try {
            if (!(drop->possibleActions() & Qt::CopyAction)) {
                drop->ignore();
                return true;
            }
            const auto path = dropped_file(*drop->mimeData());
            if (event->type() == QEvent::Drop) {
                values["file"] = display(path.string());
                selection = 0;
                values["sendStatus"] = "File selected. Choose a device, then Send.";
                emit changed();
            }
            drop->setDropAction(Qt::CopyAction);
            drop->accept();
        } catch (const std::exception& error) {
            if (event->type() == QEvent::Drop) {
                values["sendStatus"] = display(error.what());
                emit changed();
            }
            drop->ignore();
        }
        // Intercept before Quick's text editor can insert URLs or negotiate a move.
        return true;
    }
    return QObject::eventFilter(watched, event);
}
} // namespace beam::desktop
