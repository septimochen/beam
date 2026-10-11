#pragma once

#include "service.hpp"
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QMimeData;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSystemTrayIcon;

namespace beam::desktop {
// Returns exactly one validated local file. Never follows a remote URL or directory.
[[nodiscard]] std::filesystem::path dropped_file(const QMimeData& mime);
class Window : public QWidget {
    Config config;
    Service service;
    QComboBox *peers{}, *kind{};
    QLineEdit *file{}, *output{};
    QPlainTextEdit* text{};
    QLabel *send_status{}, *receive_status{}, *last_result{};
    QProgressBar *send_progress{}, *receive_progress{};
    QPushButton *send_button{}, *receive_button{}, *cancel_button{}, *folder_button{};
    QSystemTrayIcon* tray{};
    std::optional<std::string> clipboard_snapshot;
    void refresh_peers();
    void load_clipboard();
    void send_selection();
    void poll();
    void report(const Event& event);
    void show_window();

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

  public:
    explicit Window(Config configuration, const std::filesystem::path& receive_directory = {});
};
} // namespace beam::desktop
