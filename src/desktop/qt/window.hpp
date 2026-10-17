#pragma once

#include "desktop/service.hpp"
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
    QPlainTextEdit *text{}, *received_text{};
    QLabel *send_status{}, *receive_status{}, *last_result{};
    QProgressBar *send_progress{}, *receive_progress{};
    QPushButton *send_button{}, *receive_button{}, *cancel_button{}, *folder_button{};
    QPushButton* check_button{};
    QLabel* connectivity_status{};
    QLabel *tray_send_status{}, *tray_receive_status{};
    QProgressBar *tray_send_progress{}, *tray_receive_progress{};
    QSystemTrayIcon* tray{};
    std::optional<std::string> clipboard_snapshot;
    bool receive_on_launch{true};
    void start_receiving_when_ready();
    void start_receiving();
    void stop_receiving();
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
