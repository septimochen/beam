#pragma once

#include "desktop/service.hpp"
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

class QMimeData;
class QQmlApplicationEngine;
class QQuickWindow;
class QMenu;
class QSystemTrayIcon;
class QAction;
#ifdef BEAM_HAS_PAIRING
#include "beam/pairing.hpp"
#endif

namespace beam::desktop {
[[nodiscard]] std::filesystem::path dropped_file(const QMimeData& mime);
// All commands and snapshots belong to the GUI thread. The service owns workers.
class Window : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DesktopController)
    QML_UNCREATABLE("Owned by the Beam application")
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QString content READ content WRITE set_content NOTIFY changed)
    Q_PROPERTY(QString peer READ peer WRITE set_peer NOTIFY changed)
    Q_PROPERTY(int kind READ kind WRITE set_kind NOTIFY changed)
    Config config;
    Service service;
    QVariantMap values, last_snapshot;
    QString outgoing, selected_peer;
    int selection{};
    std::optional<std::string> clipboard_snapshot;
    std::optional<TrustedPeer> editing;
    std::string import_contents;
    QString checked_peer;
    QVariantMap device_checks;
    bool receive_start_pending{true}, use_default_directory{};
#ifdef BEAM_HAS_PAIRING
    std::unique_ptr<PairingSession> pairing;
    PairingState previous_pairing{PairingState::idle};
#endif
    std::unique_ptr<QMenu> menu;
    QSystemTrayIcon* tray{};
    QAction *tray_send{}, *tray_receive{};
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *main_window{}, *panel{};
    void start_receiving_when_ready();
    void restart_receiving();
    void poll();
    void report(const Event& event);
    void poll_pairing();
    void attempt(const std::function<void()>& action);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  public:
    explicit Window(Config configuration, const std::filesystem::path& receive_directory = {});
    ~Window() override;
    QVariantMap state() const;
    QString content() const { return outgoing; }
    QString peer() const { return selected_peer; }
    int kind() const { return selection; }
    void set_content(const QString& value);
    void set_peer(const QString& value);
    void set_kind(int value);
    void select_receive_directory(const std::filesystem::path& directory);
    // Window lifecycle also used by single-instance activation tests.
    void show() { show_window(); }
    void hide();
    void showMinimized();
    bool isVisible() const;
    bool isMinimized() const;
    QQuickWindow* view() const { return main_window; }
    QQuickWindow* transfer_panel() const { return panel; }
    Q_INVOKABLE void show_window();
    Q_INVOKABLE void show_share();
    Q_INVOKABLE void show_transfers();
    Q_INVOKABLE void refresh_peers();
    Q_INVOKABLE void open_devices();
    Q_INVOKABLE void choose_file();
    Q_INVOKABLE void choose_folder();
    Q_INVOKABLE void load_clipboard();
    Q_INVOKABLE void copy_text(bool received);
    Q_INVOKABLE void send_selection();
    Q_INVOKABLE void cancel_send();
    Q_INVOKABLE void check_peer(const QString& name);
    Q_INVOKABLE void begin_edit(const QString& name);
    Q_INVOKABLE bool save_device(const QVariantMap& fields);
    Q_INVOKABLE void choose_certificate();
    Q_INVOKABLE void unpair_device(const QString& name);
    Q_INVOKABLE void show_identity();
    Q_INVOKABLE void copy_certificate();
    Q_INVOKABLE void export_devices();
    Q_INVOKABLE void preview_import();
    Q_INVOKABLE bool restore_devices();
    Q_INVOKABLE void discard_import();
    Q_INVOKABLE void open_pairing();
    Q_INVOKABLE void close_pairing();
    Q_INVOKABLE void pairing_action(const QString& action, const QString& value = {});

  signals:
    void changed();
};
} // namespace beam::desktop
