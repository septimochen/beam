#include "beam/beam.hpp"
#include "beam/config.hpp"
#include "beam/identity.hpp"
#include "window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("beam-desktop");
    QApplication::setDesktopFileName("beam");
    QApplication::setApplicationVersion(QString::fromStdString(std::string(beam::version())));
    QApplication::setQuitOnLastWindowClosed(true);
    QCommandLineParser parser;
    parser.setApplicationDescription("Beam tray app for explicitly paired devices. Closing hides "
                                     "to the tray when available; use Quit Beam to stop workers.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"state-dir", "Existing Beam identity and peer store", "directory"});
    parser.addOption({"output", "Receive folder (overrides the remembered folder)", "directory"});
    parser.addOption({"listen", "Receive UDP port", "port"});
    parser.addOption({"no-discovery", "Do not advertise the receiver on the LAN"});
    parser.process(app);
    if (!parser.positionalArguments().isEmpty()) {
        std::cerr << "beam-desktop: unexpected positional arguments\n";
        return 1;
    }
    try {
        bool valid = false;
        const auto defaults = beam::load_config(beam::default_config_file());
        const auto port =
            parser.isSet("listen") ? parser.value("listen").toUInt(&valid) : defaults.listen_port;
        if (!parser.isSet("listen"))
            valid = true;
        if (!valid || port == 0 || port > 65535)
            throw std::invalid_argument("listen port must be between 1 and 65535");
        beam::desktop::Config config;
        config.state_directory =
            parser.isSet("state-dir")
                ? std::filesystem::path(parser.value("state-dir").toUtf8().toStdString())
                : beam::default_state_directory();
        config.port = static_cast<std::uint16_t>(port);
        config.advertise = defaults.discovery && !parser.isSet("no-discovery");
        config.timeout = defaults.timeout;
        beam::desktop::Window window(
            config, parser.isSet("output")
                        ? std::filesystem::path(parser.value("output").toUtf8().toStdString())
                        : defaults.receive_folder);
        window.show();
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << "beam-desktop: " << error.what() << '\n';
        return 1;
    }
}
