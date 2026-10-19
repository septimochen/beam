#include "desktop/qt/single_instance.hpp"
#include "desktop/qt/window.hpp"
#include <QApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
class Process : public QProcess {
  public:
    ~Process() override {
        if (state() != QProcess::NotRunning) {
            kill();
            waitForFinished(5000);
        }
    }
    QByteArray line() {
        while (!canReadLine())
            require(waitForReadyRead(8000), "timed out waiting for child process output");
        return readLine().trimmed();
    }
};
int serve(const QString& mode, const QString& directory) {
    beam::desktop::SingleInstance instance;
    if (instance.activate_existing()) {
        std::cout << "secondary" << std::endl;
        return 0;
    }
    beam::desktop::Config config;
    config.state_directory = (directory + "/state").toStdString();
    config.advertise = false;
    beam::desktop::Window window(config, directory.toStdString());
    QObject::connect(&instance, &beam::desktop::SingleInstance::activation_requested, &window, [&] {
        const bool token_forwarded = qgetenv("XDG_ACTIVATION_TOKEN") == "beam-test-token";
        window.show_window();
        std::cout << (window.isVisible() && !window.isMinimized() && token_forwarded ? "activated"
                                                                                     : "failed")
                  << std::endl;
        if (mode == "quit")
            QTimer::singleShot(0, qApp, &QApplication::quit);
    });
    window.show();
    if (mode == "minimized")
        window.showMinimized();
    else
        window.hide();
    std::cout << "primary" << std::endl;
    QTimer::singleShot(20000, qApp, &QApplication::quit);
    return QApplication::exec();
}
void launch_again(const QString& executable) {
    Process secondary;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_ACTIVATION_TOKEN", "beam-test-token");
    secondary.setProcessEnvironment(environment);
    // Activate before reading configuration or starting workers in the second process.
    secondary.start(executable, {"--state-dir", "/nonexistent/beam-instance-test", "--output",
                                 "/nonexistent/beam-instance-test"});
    require(secondary.waitForFinished(8000), "second launch did not exit");
    require(secondary.exitStatus() == QProcess::NormalExit && secondary.exitCode() == 0,
            "second launch failed to activate the primary");
}
void tests(const QString& executable) {
    {
        beam::desktop::SingleInstance instance;
        require(!instance.activate_existing(), "cannot acquire isolated test service");
        QByteArray observed;
        QObject::connect(&instance, &beam::desktop::SingleInstance::activation_requested, &instance,
                         [&] { observed = qgetenv("XDG_ACTIVATION_TOKEN"); });
        qputenv("XDG_ACTIVATION_TOKEN", "original");
        instance.Activate(QString(4097, 'a'));
        require(observed == "original", "oversized activation token was accepted");
        instance.Activate(QString(QChar::Null));
        require(observed == "original", "activation token containing NUL was accepted");
        instance.Activate("beam-test-token");
        require(observed == "beam-test-token" && !qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN"),
                "activation token was not forwarded and cleared");
    }
    {
        Process unavailable;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/beam-session-bus");
        unavailable.setProcessEnvironment(environment);
        unavailable.start(executable, {});
        require(unavailable.waitForFinished(5000), "missing session bus did not stop launch");
        require(unavailable.exitStatus() == QProcess::NormalExit && unavailable.exitCode() == 1 &&
                    unavailable.readAllStandardError().contains("desktop session bus"),
                "missing session bus did not produce an actionable error");
    }
    QTemporaryDir directory;
    require(directory.isValid(), "cannot create temporary test directory");
    for (const auto* mode : {"hidden", "minimized", "quit"}) {
        Process primary;
        primary.start(QApplication::applicationFilePath(), {"--serve", mode, directory.path()});
        require(primary.line() == "primary", "first launch did not acquire the service");
        launch_again(executable);
        require(primary.line() == "activated", "second launch did not restore the existing window");
        if (QString(mode) == "quit") {
            require(primary.state() == QProcess::NotRunning || primary.waitForFinished(5000),
                    "primary did not quit cleanly");
            require(primary.exitCode() == 0, "primary shutdown failed");
        } else {
            launch_again(executable);
            require(primary.line() == "activated", "repeated launch did not activate the window");
            primary.kill();
            require(primary.waitForFinished(5000), "cannot stop primary");
        }
    }
    // Concurrent startup must elect one owner, including after clean shutdown.
    Process first;
    Process second;
    first.start(QApplication::applicationFilePath(), {"--serve", "hidden", directory.path()});
    second.start(QApplication::applicationFilePath(), {"--serve", "hidden", directory.path()});
    const auto first_result = first.line();
    const auto second_result = second.line();
    require((first_result == "primary" && second_result == "secondary") ||
                (first_result == "secondary" && second_result == "primary"),
            "concurrent launches did not elect exactly one primary");
    auto& secondary = first_result == "secondary" ? first : second;
    require(secondary.state() == QProcess::NotRunning || secondary.waitForFinished(5000),
            "concurrent secondary did not exit");
    require(secondary.exitCode() == 0, "concurrent activation failed");
}
} // namespace
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 4 && args[1] == "--serve")
            return serve(args[2], args[3]);
        require(args.size() == 2, "expected desktop executable path");
        tests(args[1]);
        std::cout << "desktop single-instance tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
