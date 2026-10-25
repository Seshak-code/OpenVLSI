// OpenVLSI: the CIW and the Library Manager over a workspace. `OpenVLSI [workspace.libs]`; without an argument the
// last workspace opened, else ~/OpenVLSI/openvlsi.libs.
#include "app/ciw.h"
#include "app/library_manager.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

#include <exception>
#include <memory>

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    QApplication::setOrganizationName("OpenVLSI");
    QApplication::setApplicationName("OpenVLSI");
    QSettings settings;
    QString file = qapp.arguments().value(1);
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
    app::Ciw ciw(*session);
    ciw.show();
    ciw.libraryManager()->show();
    return qapp.exec();
}
