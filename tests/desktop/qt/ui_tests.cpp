#include "desktop/qt/window.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QUrl>
#include <fstream>
#include <iostream>

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
void tests() {
    QTemporaryDir temporary;
    require(temporary.isValid(), "cannot create UI fixture");
    const auto root = std::filesystem::canonical(temporary.path().toUtf8().toStdString());
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
    auto* content = window.findChild<QPlainTextEdit*>("content");
    auto* load = window.findChild<QPushButton*>("loadClipboard");
    auto* send = window.findChild<QPushButton*>("send");
    auto* kind = window.findChild<QComboBox*>("kind");
    require(content && load && send && kind, "missing desktop controls");
    require(content->toPlainText().isEmpty(), "startup read the clipboard");
    require(!send->isEnabled(), "unpaired send enabled");
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
