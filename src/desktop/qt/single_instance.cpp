#include "single_instance.hpp"
#include <QDBusError>
#include <QDBusMessage>
#include <stdexcept>

namespace beam::desktop {
namespace {
constexpr auto service = "org.beam.Desktop";
constexpr auto path = "/org/beam/Desktop";
} // namespace

SingleInstance::SingleInstance() {
    if (!bus.isConnected())
        throw std::runtime_error("cannot connect to the desktop session bus: " +
                                 bus.lastError().message().toStdString());
    // Export first so a simultaneous launch cannot address an unregistered object.
    if (!bus.registerObject(path, this, QDBusConnection::ExportScriptableSlots))
        throw std::runtime_error("cannot register desktop activation: " +
                                 bus.lastError().message().toStdString());
}
SingleInstance::~SingleInstance() {
    if (primary)
        bus.unregisterService(service);
    bus.unregisterObject(path);
}
bool SingleInstance::activate_existing() {
    // Name acquisition is atomic, without replacement or queuing for ownership.
    primary = bus.registerService(service);
    if (primary)
        return false;
    auto request = QDBusMessage::createMethodCall(service, path, service, "Activate");
    request << QString::fromUtf8(qgetenv("XDG_ACTIVATION_TOKEN"));
    const auto reply = bus.call(request, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage)
        throw std::runtime_error("cannot activate the running Beam window: " +
                                 reply.errorMessage().toStdString());
    return true;
}
void SingleInstance::Activate(const QString& activation_token) {
    // Qt's Wayland backend consumes this token when activateWindow() is called.
    // Only the fixed activation variable is forwarded, never launch configuration.
    const bool valid_token = !activation_token.isEmpty() && activation_token.size() <= 4096 &&
                             !activation_token.contains(QChar::Null);
    if (valid_token)
        qputenv("XDG_ACTIVATION_TOKEN", activation_token.toUtf8());
    emit activation_requested();
    if (valid_token)
        qunsetenv("XDG_ACTIVATION_TOKEN");
}
} // namespace beam::desktop
