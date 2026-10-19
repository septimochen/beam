#pragma once

#include <QDBusConnection>
#include <QObject>

namespace beam::desktop {
// Owns the desktop session's Beam service before any window or worker is created.
class SingleInstance : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.beam.Desktop")
    QDBusConnection bus{QDBusConnection::sessionBus()};
    bool primary{};

  public:
    SingleInstance();
    ~SingleInstance() override;
    // Call once: returns true after the existing instance acknowledges activation.
    [[nodiscard]] bool activate_existing();

  public slots:
    Q_SCRIPTABLE void Activate(const QString& activation_token);

  signals:
    void activation_requested();
};
} // namespace beam::desktop
