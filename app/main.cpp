// OpenVLSI: the one window over a workspace. `OpenVLSI [workspace.libs]`; without an argument the last workspace
// opened, else ~/OpenVLSI/openvlsi.libs.
#include "app/shell.h"
#include "app/ui_commands.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <exception>
#include <memory>

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    QApplication::setOrganizationName("OpenVLSI");
    QApplication::setApplicationName("OpenVLSI");
    QSettings settings;
    app::Theme::Mode theme = app::Theme::Mode::Auto;
    app::Theme::parseMode(settings.value("theme", "auto").toString(), &theme);
    app::Theme::apply(qapp, theme);
    // --run LINE (repeatable): command-window lines to run once the window is up, a moment apart; "quit" ends the app.
    // Drives the real GUI from a script.
    QStringList args = qapp.arguments().mid(1), lines;
    while (args.size() >= 2 && args[0] == "--run") {
        lines << args[1];
        args = args.mid(2);
    }
    // --window-menus: the menu bar inside the window, not the system's: its items have screen rectangles a test can click
    if (args.value(0) == "--window-menus") {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
        args = args.mid(1);
    }
    // --dump-api FILE: the API's reference, generated from the commands themselves
    QString dumpApi;
    if (args.size() >= 2 && args[0] == "--dump-api") {
        dumpApi = args[1];
        args = args.mid(2);
    }
    QString file = args.value(0);
    if (file.isEmpty()) file = settings.value("workspace", QDir::homePath() + "/OpenVLSI/openvlsi.libs").toString();
    file = QFileInfo(file).absoluteFilePath();
    QDir().mkpath(QFileInfo(file).absolutePath());

    std::unique_ptr<app::Session> session;
    try {
        session = std::make_unique<app::Session>(file);
    } catch (const std::exception& e) {
        if (!dumpApi.isEmpty() || qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") std::fprintf(stderr, "OpenVLSI: %s\n", e.what());  // no one is there to dismiss a box
        else QMessageBox::critical(nullptr, "OpenVLSI", QString::fromUtf8(e.what()));
        return 1;
    }
    settings.setValue("workspace", file);
    app::Shell shell(*session);
    if (!dumpApi.isEmpty()) return app::writeApiReference(*session, dumpApi) ? 0 : 1;
    shell.show();
    int delay = 800;
    for (const QString& line : lines) {
        QTimer::singleShot(delay, &shell, [&shell, line] {
            if (line == "quit") QApplication::quit();
            else shell.ciw()->runLine(line);
        });
        delay += 700;
    }
    return qapp.exec();
}
