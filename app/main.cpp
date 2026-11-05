// OpenVLSI: the one window over a workspace. `OpenVLSI [workspace.libs]`; without an argument the last workspace
// opened, else ~/OpenVLSI/openvlsi.libs.
#include "app/shell.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

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
    QString file = args.value(0);
    if (file.isEmpty()) file = settings.value("workspace", QDir::homePath() + "/OpenVLSI/openvlsi.libs").toString();
    file = QFileInfo(file).absoluteFilePath();
    QDir().mkpath(QFileInfo(file).absolutePath());

    std::unique_ptr<app::Session> session;
    try {
        session = std::make_unique<app::Session>(file);
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "OpenVLSI", QString::fromUtf8(e.what()));
        return 1;
    }
    settings.setValue("workspace", file);
    app::Shell shell(*session);
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
