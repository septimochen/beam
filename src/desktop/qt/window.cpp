#include "window.hpp"
#include "beam/identity.hpp"
#include "devices_dialog.hpp"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace beam::desktop {
namespace {
QString display(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
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
void plain(QLabel* label) {
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
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
    : config(std::move(configuration)), service(config) {
    setWindowTitle("Beam");
    setWindowIcon(icon());
    resize(640, 580);
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);
    auto* title = new QLabel("Beam", this);
    auto font = title->font();
    font.setPointSize(24);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    auto* subtitle = new QLabel("Share directly with your paired devices.", this);
    layout->addWidget(subtitle);

    auto* send_group = new QGroupBox("Send", this);
    auto* send_layout = new QVBoxLayout(send_group);
    auto* peer_row = new QHBoxLayout;
    auto* peer_label = new QLabel("Device", this);
    peers = new QComboBox(this);
    peers->setObjectName("peers");
    peer_label->setBuddy(peers);
    auto* refresh = new QPushButton("Refresh devices", this);
    refresh->setObjectName("refreshPeers");
    peer_row->addWidget(peer_label);
    peer_row->addWidget(peers, 1);
    peer_row->addWidget(refresh);
    auto* manage = new QPushButton("Manage devices…", this);
    manage->setObjectName("manageDevices");
    peer_row->addWidget(manage);
    connect(manage, &QPushButton::clicked, this, [this] {
        show_devices_dialog(
            this, config, service,
            [this] {
                poll();
                refresh_peers();
            },
            connectivity_status);
    });
    send_layout->addLayout(peer_row);
    check_button = new QPushButton("Check connectivity", this);
    check_button->setObjectName("checkConnectivity");
    connectivity_status = new QLabel(this);
    connectivity_status->setObjectName("connectivityStatus");
    plain(connectivity_status);
    send_layout->addWidget(check_button);
    send_layout->addWidget(connectivity_status);
    connect(check_button, &QPushButton::clicked, this, [this] {
        try {
            service.check_peer(bytes(peers->currentText()));
            connectivity_status->setText("Checking…");
            check_button->setEnabled(false);
        } catch (const std::exception& error) {
            connectivity_status->setText(display(error.what()));
        }
    });
    connect(refresh, &QPushButton::clicked, this, [this] { refresh_peers(); });

    auto* selection_row = new QHBoxLayout;
    kind = new QComboBox(this);
    kind->setObjectName("kind");
    kind->addItems({"File", "Text"});
    kind->setAccessibleName("Content type");
    auto* choose = new QPushButton("Choose file…", this);
    auto* clipboard = new QPushButton("Load clipboard text", this);
    clipboard->setObjectName("loadClipboard");
    selection_row->addWidget(kind);
    selection_row->addWidget(choose);
    selection_row->addWidget(clipboard);
    send_layout->addLayout(selection_row);
    file = new QLineEdit(this);
    file->setObjectName("file");
    file->setReadOnly(true);
    file->setPlaceholderText("Choose a file or drop it into this window");
    file->setAccessibleName("Selected file");
    send_layout->addWidget(file);
    text = new QPlainTextEdit(this);
    text->setObjectName("content");
    text->setPlaceholderText("Review the text or URL before sending");
    text->setAccessibleName("Content to send");
    text->setMaximumHeight(100);
    text->hide();
    connect(text, &QPlainTextEdit::textChanged, this, [this] { clipboard_snapshot.reset(); });
    send_layout->addWidget(text);
    auto* copy_text = new QPushButton("Copy to clipboard", this);
    auto* text_header = new QHBoxLayout;
    text_header->addStretch();
    text_header->addWidget(copy_text);
    send_layout->insertLayout(send_layout->indexOf(text), text_header);
    copy_text->hide();
    connect(kind, &QComboBox::currentIndexChanged, this,
            [copy_text](int index) { copy_text->setVisible(index != 0); });
    connect(copy_text, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(text->toPlainText()); });
    connect(kind, &QComboBox::currentIndexChanged, this, [this](int index) {
        file->setVisible(index == 0);
        text->setVisible(index != 0);
    });
    connect(choose, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Choose one file");
        if (!path.isEmpty()) {
            try {
                validate_request({"selection", ContentKind::file, bytes(path), {}});
                file->setText(path);
                kind->setCurrentIndex(0);
            } catch (const std::exception& error) {
                send_status->setText(display(error.what()));
            }
        }
    });
    connect(clipboard, &QPushButton::clicked, this, [this] { load_clipboard(); });
    auto* privacy = new QLabel(
        "Clipboard text is read only when you click Load. Nothing is sent until you click Send.",
        this);
    plain(privacy);
    send_layout->addWidget(privacy);
    auto* buttons = new QHBoxLayout;
    send_button = new QPushButton("Send", this);
    send_button->setObjectName("send");
    cancel_button = new QPushButton("Cancel send", this);
    cancel_button->setEnabled(false);
    buttons->addWidget(send_button);
    buttons->addWidget(cancel_button);
    buttons->addStretch();
    send_layout->addLayout(buttons);
    connect(send_button, &QPushButton::clicked, this, [this] { send_selection(); });
    connect(cancel_button, &QPushButton::clicked, this, [this] { service.cancel_send(); });
    send_progress = new QProgressBar(this);
    send_progress->setRange(0, 100);
    send_progress->setValue(0);
    send_progress->setAccessibleName("Send progress");
    send_layout->addWidget(send_progress);
    send_status = new QLabel("Choose a paired device and content.", this);
    send_status->setObjectName("sendStatus");
    plain(send_status);
    send_layout->addWidget(send_status);
    layout->addWidget(send_group);

    auto* receive_group = new QGroupBox("Receive", this);
    auto* receive_layout = new QVBoxLayout(receive_group);
    auto* folder_row = new QHBoxLayout;
    output = new QLineEdit(receive_directory.empty()
                               ? QSettings("Beam", "Beam").value("receiveDirectory").toString()
                               : display(receive_directory.string()),
                           this);
    use_default_directory = output->text().isEmpty();
    if (use_default_directory)
        output->setText(QDir::home().filePath("Downloads"));
    output->setObjectName("output");
    output->setReadOnly(true);
    output->setPlaceholderText("Choose where received content is saved");
    output->setAccessibleName("Receive folder");
    auto* folder_button = new QPushButton("Choose folder…", this);
    folder_row->addWidget(output, 1);
    folder_row->addWidget(folder_button);
    receive_layout->addLayout(folder_row);
    connect(folder_button, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Receive folder", output->text());
        select_receive_directory(bytes(path));
    });
    receive_progress = new QProgressBar(this);
    receive_progress->setRange(0, 100);
    receive_progress->setValue(0);
    receive_progress->setAccessibleName("Receive progress");
    receive_layout->addWidget(receive_progress);
    receive_status = new QLabel("Stopped.", this);
    receive_status->setObjectName("receiveStatus");
    plain(receive_status);
    receive_layout->addWidget(receive_status);
    auto* received_header = new QHBoxLayout;
    received_header->addWidget(new QLabel("Received text", this));
    received_header->addStretch();
    auto* copy_received = new QPushButton("Copy to clipboard", this);
    copy_received->setObjectName("copyReceived");
    received_header->addWidget(copy_received);
    receive_layout->addLayout(received_header);
    received_text = new QPlainTextEdit(this);
    received_text->setObjectName("receivedText");
    received_text->setReadOnly(true);
    received_text->setMaximumHeight(120);
    receive_layout->addWidget(received_text);
    connect(copy_received, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(received_text->toPlainText()); });
    layout->addWidget(receive_group);
    last_result = new QLabel("No completed transfers yet.", this);
    last_result->setObjectName("lastResult");
    plain(last_result);
    layout->addWidget(last_result);
    auto* hint = new QLabel("Choose Add device to pair on your local network.", this);
    plain(hint);
    layout->addWidget(hint);

    tray = new QSystemTrayIcon(icon(), this);
    tray->setToolTip("Beam");
    auto* menu = new QMenu(this);
    auto* tray_content = new QWidget(menu);
    tray_content->setMinimumWidth(280);
    tray_content->setMaximumWidth(360);
    auto* tray_layout = new QVBoxLayout(tray_content);
    tray_send_status = new QLabel("Send: idle", tray_content);
    tray_receive_status = new QLabel("Receive: idle", tray_content);
    plain(tray_send_status);
    plain(tray_receive_status);
    tray_send_progress = new QProgressBar(tray_content);
    tray_receive_progress = new QProgressBar(tray_content);
    for (auto* progress : {tray_send_progress, tray_receive_progress}) {
        progress->setRange(0, 100);
        progress->setValue(0);
    }
    tray_layout->addWidget(tray_send_status);
    tray_layout->addWidget(tray_send_progress);
    tray_layout->addWidget(tray_receive_status);
    tray_layout->addWidget(tray_receive_progress);
    auto* progress_action = new QWidgetAction(menu);
    progress_action->setDefaultWidget(tray_content);
    menu->addAction(progress_action);
    menu->addSeparator();
    connect(menu->addAction("Open Beam"), &QAction::triggered, this, [this] { show_window(); });
    connect(menu->addAction("Load clipboard text…"), &QAction::triggered, this, [this] {
        show_window();
        load_clipboard();
    });
    menu->addSeparator();
    connect(menu->addAction("Quit Beam"), &QAction::triggered, qApp, &QApplication::quit);
    tray->setContextMenu(menu);
    connect(tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    show_window();
            });
    tray->show();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { poll(); });
    timer->start(100);
    refresh_peers();
}
void Window::show_window() {
    showNormal();
    raise();
    activateWindow();
}
void Window::refresh_peers() {
    const auto selected = peers->currentText();
    peers->clear();
    try {
        IdentityStore store(config.state_directory);
        for (const auto& peer : store.peers())
            peers->addItem(display(peer.name));
        const auto index = peers->findText(selected);
        if (index >= 0)
            peers->setCurrentIndex(index);
        if (peers->count() == 0)
            send_status->setText("No paired devices. Choose Add device to pair.");
    } catch (const std::exception& error) {
        send_status->setText("Pairing setup needed: " + display(error.what()));
    }
    send_button->setEnabled(peers->count() > 0 && !service.is_sending());
    restart_receiving();
}
void Window::start_receiving_when_ready() {
    if (!receive_start_pending || service.is_receiving())
        return;
    receive_start_pending = false;
    if (peers->count() == 0) {
        receive_status->setText("Choose Add device to pair a device.");
        return;
    }
    start_receiving();
}
void Window::start_receiving() {
    try {
        if (use_default_directory)
            std::filesystem::create_directory(bytes(output->text()));
        service.start_receiving(bytes(output->text()));
        receive_status->setText("Starting…");
    } catch (const std::exception& error) {
        receive_status->setText("Transfer stopped: " + display(error.what()));
    }
}
void Window::restart_receiving() {
    receive_start_pending = true;
    if (service.is_receiving()) {
        service.stop_receiving();
        receive_status->setText("Updating receive settings…");
    } else {
        start_receiving_when_ready();
    }
}
void Window::select_receive_directory(const std::filesystem::path& directory) {
    if (directory.empty())
        return;
    output->setText(display(directory.string()));
    QSettings("Beam", "Beam").setValue("receiveDirectory", output->text());
    use_default_directory = false;
    restart_receiving();
}
void Window::load_clipboard() {
    try {
        // No clipboard watchers, startup reads, PRIMARY selection, or clipboard writes.
        const auto* mime = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
        if (!mime || !mime->hasText())
            throw std::invalid_argument("Clipboard has no text; choose a file for other content");
        const auto snapshot = mime->text();
        if (snapshot.size() > static_cast<qsizetype>(max_text_size))
            throw std::invalid_argument("Clipboard text exceeds 64 KiB");
        const auto content = bytes(snapshot);
        validate_content(ContentKind::text, content);
        text->setPlainText(snapshot);
        clipboard_snapshot = content;
        kind->setCurrentIndex(1);
        send_status->setText("Clipboard snapshot loaded. Review it, choose a device, then Send.");
    } catch (const std::exception& error) {
        send_status->setText(display(error.what()));
    }
}
void Window::send_selection() {
    try {
        SendRequest request;
        request.peer = bytes(peers->currentText());
        request.kind = static_cast<ContentKind>(kind->currentIndex() + 1);
        request.file = bytes(file->text());
        if (request.kind != ContentKind::file) {
            const auto value = text->toPlainText();
            if (value.size() > static_cast<qsizetype>(max_text_size))
                throw std::invalid_argument("Text exceeds 64 KiB");
            request.content = clipboard_snapshot.value_or(bytes(value));
        }
        service.send(std::move(request));
        send_button->setEnabled(false);
        cancel_button->setEnabled(true);
        send_progress->setValue(0);
        send_status->setText("Sending…");
    } catch (const std::exception& error) {
        send_status->setText(display(error.what()));
    }
}
void Window::poll() {
    for (const auto& event : service.take_events())
        report(event);
    send_button->setEnabled(!service.is_sending() && peers->count() > 0);
    cancel_button->setEnabled(service.is_sending());
    if (receive_start_pending) {
        receive_status->setText("Updating receive settings…");
        start_receiving_when_ready();
    }
    check_button->setEnabled(peers->count() > 0 && !service.is_checking());
    tray_send_status->setText("Send: " + send_status->text());
    tray_receive_status->setText("Receive: " + receive_status->text());
    for (const auto& pair : {std::pair{send_progress, tray_send_progress},
                             std::pair{receive_progress, tray_receive_progress}}) {
        pair.second->setRange(pair.first->minimum(), pair.first->maximum());
        pair.second->setValue(pair.first->value());
    }
}
void Window::report(const Event& event) {
    if (event.direction == Direction::check) {
        connectivity_status->setText(display(event.message));
        return;
    }
    auto* status = event.direction == Direction::send ? send_status : receive_status;
    auto* progress = event.direction == Direction::send ? send_progress : receive_progress;
    QString message;
    switch (event.kind) {
    case EventKind::progress: {
        const auto& update = event.progress;
        const bool counted =
            update.stage == TransferStage::hashing || update.stage == TransferStage::transferring;
        progress->setRange(0, counted ? 100 : 0);
        if (counted)
            progress->setValue(
                update.total == 0
                    ? 0
                    : static_cast<int>(100.0L * static_cast<long double>(update.bytes) /
                                       static_cast<long double>(update.total)));
        auto label = stage_name(update.stage);
        if (update.stage == TransferStage::transferring)
            label += QString(" · %1% · %2 MiB/s")
                         .arg(progress->value())
                         .arg(update.bytes_per_second / 1048576, 0, 'f', 1);
        status->setText(label);
        return;
    }
    case EventKind::listening:
        status->setText(QString("Listening for paired devices on port %1.").arg(config.port));
        return;
    case EventKind::completed:
        if (event.direction == Direction::receive && event.result.kind == ContentKind::text)
            received_text->setPlainText(display(event.result.text));
        progress->setRange(0, 100);
        progress->setValue(100);
        message = event.direction == Direction::send
                      ? "Sent and verified with " + display(event.message)
                      : "Received and verified " + display(event.result.filename);
        message += QString(" (%1 bytes)").arg(static_cast<qulonglong>(event.result.bytes));
        break;
    case EventKind::failed:
        progress->setRange(0, 100);
        progress->setValue(0);
        message = "Transfer stopped: " + display(event.message);
        break;
    case EventKind::warning:
        status->setText("LAN advertisement unavailable: " + display(event.message));
        return;
    case EventKind::stopped:
        progress->setRange(0, 100);
        progress->setValue(0);
        // Preserve any useful startup failure instead of replacing it with Stopped.
        if (!status->text().startsWith("Transfer stopped:"))
            status->setText("Receiver stopped.");
        return;
    }
    status->setText(message);
    last_result->setText(message);
    if (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages())
        tray->showMessage("Beam", message,
                          event.kind == EventKind::failed ? QSystemTrayIcon::Warning
                                                          : QSystemTrayIcon::Information);
}
void Window::dragEnterEvent(QDragEnterEvent* event) {
    try {
        static_cast<void>(dropped_file(*event->mimeData()));
        event->acceptProposedAction();
    } catch (const std::exception&) {
        event->ignore();
    }
}
void Window::dropEvent(QDropEvent* event) {
    try {
        file->setText(display(dropped_file(*event->mimeData()).string()));
        kind->setCurrentIndex(0);
        send_status->setText("File selected. Choose a device, then Send.");
        event->acceptProposedAction();
    } catch (const std::exception& error) {
        send_status->setText(display(error.what()));
        event->ignore();
    }
}
void Window::closeEvent(QCloseEvent* event) {
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}
} // namespace beam::desktop
