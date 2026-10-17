#include "window.hpp"
#include "beam/identity.hpp"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
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
    send_layout->addLayout(peer_row);
    connect(refresh, &QPushButton::clicked, this, [this] { refresh_peers(); });

    auto* selection_row = new QHBoxLayout;
    kind = new QComboBox(this);
    kind->setObjectName("kind");
    kind->addItems({"File", "Text", "URL"});
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
    output->setObjectName("output");
    output->setReadOnly(true);
    output->setPlaceholderText("Choose where received content is saved");
    output->setAccessibleName("Receive folder");
    folder_button = new QPushButton("Choose folder…", this);
    folder_row->addWidget(output, 1);
    folder_row->addWidget(folder_button);
    receive_layout->addLayout(folder_row);
    connect(folder_button, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Receive folder", output->text());
        if (!path.isEmpty()) {
            output->setText(path);
            QSettings("Beam", "Beam").setValue("receiveDirectory", path);
            receive_on_launch = true;
            start_receiving_when_ready();
        }
    });
    receive_button = new QPushButton("Start receiving", this);
    receive_button->setObjectName("receive");
    receive_layout->addWidget(receive_button);
    connect(receive_button, &QPushButton::clicked, this, [this] {
        if (service.is_receiving())
            stop_receiving();
        else
            start_receiving();
    });
    receive_progress = new QProgressBar(this);
    receive_progress->setRange(0, 100);
    receive_progress->setValue(0);
    receive_progress->setAccessibleName("Receive progress");
    receive_layout->addWidget(receive_progress);
    receive_status = new QLabel("Stopped. Received text and URLs are saved as files.", this);
    receive_status->setObjectName("receiveStatus");
    plain(receive_status);
    receive_layout->addWidget(receive_status);
    layout->addWidget(receive_group);
    last_result = new QLabel("No completed transfers yet.", this);
    last_result->setObjectName("lastResult");
    plain(last_result);
    layout->addWidget(last_result);
    auto* hint = new QLabel(
        "Set up identity, pairing, and addresses with the beam CLI. Then refresh devices.", this);
    plain(hint);
    layout->addWidget(hint);

    tray = new QSystemTrayIcon(icon(), this);
    tray->setToolTip("Beam");
    auto* menu = new QMenu(this);
    connect(menu->addAction("Open Beam"), &QAction::triggered, this, [this] { show_window(); });
    connect(menu->addAction("Load clipboard text…"), &QAction::triggered, this, [this] {
        show_window();
        load_clipboard();
    });
    connect(menu->addAction("Stop receiving"), &QAction::triggered, this,
            [this] { stop_receiving(); });
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
            send_status->setText("No paired devices. Pair using the beam CLI, then refresh.");
    } catch (const std::exception& error) {
        send_status->setText("Pairing setup needed: " + display(error.what()));
    }
    send_button->setEnabled(peers->count() > 0 && !service.is_sending());
    start_receiving_when_ready();
}
void Window::start_receiving_when_ready() {
    if (!receive_on_launch || service.is_receiving())
        return;
    if (peers->count() == 0) {
        receive_status->setText(
            "Set up identity and pair a device with the beam CLI, then refresh devices.");
        return;
    }
    if (output->text().isEmpty()) {
        receive_status->setText("Choose a receive folder once to start receiving automatically.");
        return;
    }
    start_receiving();
}
void Window::start_receiving() {
    receive_on_launch = false;
    try {
        if (output->text().isEmpty())
            throw std::invalid_argument("Choose a receive folder first");
        service.start_receiving(bytes(output->text()));
        receive_status->setText("Starting…");
    } catch (const std::exception& error) {
        receive_status->setText(display(error.what()));
    }
    poll();
}
void Window::stop_receiving() {
    receive_on_launch = false;
    service.stop_receiving();
    if (service.is_receiving())
        receive_status->setText("Stopping…");
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
    receive_button->setText(service.is_receiving() ? "Stop receiving" : "Start receiving");
    folder_button->setEnabled(!service.is_receiving());
}
void Window::report(const Event& event) {
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
        status->setText(stage_name(update.stage));
        return;
    }
    case EventKind::listening:
        status->setText(QString("Listening for paired devices on port %1.").arg(config.port));
        return;
    case EventKind::completed:
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
