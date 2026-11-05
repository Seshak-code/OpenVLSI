// test_api: nothing in the window without its command. One of each tool is opened; every menu item and button in it, and in the
// shell, must name a command that exists. A new control with no API fails here. docs/api.md must list every command:
// OpenVLSI --dump-api docs/api.md regenerates it.
#include "app/ade_explorer.h"
#include "app/implement.h"
#include "app/layout_viewer.h"
#include "app/shell.h"
#include "app/test_stage.h"
#include "app/verify_stage.h"

#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unistd.h>

#ifndef OPENVLSI_SOURCE
#error "built with the source directory, for docs/api.md"
#endif

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    QApplication::setOrganizationName("OpenVLSI-test");
    QSettings().clear();
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("apitest" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::current_path(dir);
    {
        app::Theme::apply(qapp, app::Theme::Mode::Dark);
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Shell shell(session);
        const auto must = [&](const QString& c, const QJsonObject& a) {
            const cmd::Result r = session.call(c, a);
            if (!r.ok) std::printf("  (%s: %s)\n", c.toUtf8().constData(), r.error.toUtf8().constData());
            return r.ok;
        };
        must("library_create", {{"library", "l"}, {"path", "l.oalib"}});
        must("cell_create", {{"library", "l"}, {"cell", "c"}});
        for (const auto& [view, type] : {std::pair{"schematic", "schematic"}, {"symbol", "symbol"}, {"layout", "layout"}, {"ade", "maestro"}, {"verify", "verifier"}}) {
            must("view_create", {{"library", "l"}, {"cell", "c"}, {"view", view}, {"type", type}});
            must("editor_open", {{"library", "l"}, {"cell", "c"}, {"view", view}});
        }
        // A run of each stage, so their tabs open: records written by hand, as test_implement does
        const auto writeRecord = [&](const fs::path& d, const QString& json) {
            fs::create_directories(d);
            std::ofstream(d / "run.json") << json.toStdString();
        };
        writeRecord(dir / "implementation" / "g" / "run.1", R"({"run":"g/run.1","design":"g","top":"g","platform":"ihp-sg13g2","state":"done","stage":null,"stages":[],"pid":1,"started":1})");
        writeRecord(dir / "testing" / "t" / "run.1", R"({"run":"t/run.1","name":"t","kind":"testbench","level":"rtl","state":"done","passed":true,"started":1})");
        must("flow_view", {{"run", "g/run.1"}});
        must("test_view", {{"run", "t/run.1"}});
        must("timing_view", {{"run", "g/run.1"}});
        must("power_view", {{"run", "g/run.1"}});
        std::ofstream(dir / "a.v") << "module a; endmodule\n";
        must("rtl_open", {{"file", QString::fromStdString((dir / "a.v").string())}});
        must("ui_library_manager", {});

        const auto audit = [&](QWidget* w, const QString& name) {
            std::set<QString> missing, unknown;
            for (QAction* a : w->findChildren<QAction*>()) {
                if (a->isSeparator() || a->text().isEmpty() || a->menu()) continue;
                // A dock's or toolbar's own show-me entry: no menu offers it, and its panel has its commands
                if (auto* dock = qobject_cast<QDockWidget*>(a->parent()); dock && a == dock->toggleViewAction()) continue;
                if (auto* bar = qobject_cast<QToolBar*>(a->parent()); bar && a == bar->toggleViewAction()) continue;
                const QString api = a->property("api").toString();
                if (api.isEmpty()) missing.insert(QString(a->text()).remove('&'));
                else if (!session.registry().find(api)) unknown.insert(api);
            }
            for (QPushButton* b : w->findChildren<QPushButton*>()) {
                if (b->text().isEmpty()) continue;
                const QString api = b->property("api").toString();
                if (api.isEmpty()) missing.insert(b->text());
                else if (!session.registry().find(api)) unknown.insert(api);
            }
            check(missing.empty(), (name + ": every control names its command" + (missing.empty() ? QString() : ": missing " + QStringList(missing.begin(), missing.end()).join(", "))).toStdString());
            check(unknown.empty(), (name + ": every command named exists" + (unknown.empty() ? QString() : ": no " + QStringList(unknown.begin(), unknown.end()).join(", "))).toStdString());
        };
        audit(&shell, "the whole window");
        for (int k = 0; k < shell.tabs()->count(); ++k) audit(shell.tabs()->widget(k), shell.tabs()->tabText(k));
        audit(shell.ciw(), "the command window");
        check(shell.tabs()->count() >= 8, "one of each tool was open: " + std::to_string(shell.tabs()->count()) + " tabs");

        // Accessibility: every control and every canvas a screen reader meets says what it is
        QStringList unnamed;
        for (QWidget* w : shell.findChildren<QWidget*>()) {
            const auto* tool = qobject_cast<QToolButton*>(w);
            const bool custom = w->inherits("app::LayoutCanvas") || w->inherits("app::Canvas") || w->inherits("app::Plot") || w->inherits("app::DigitalWaves") || w->inherits("app::StagePipeline");
            if ((tool && tool->text().isEmpty() && !tool->objectName().startsWith("qt_") && !w->inherits("QTabBar") && !QString(w->parent()->metaObject()->className()).startsWith("QTab") && !qobject_cast<QLineEdit*>(w->parent())) || custom)
                if (w->accessibleName().isEmpty()) unnamed << QString(w->metaObject()->className());
        }
        check(unnamed.isEmpty(), ("every icon button and canvas has an accessible name" + (unnamed.isEmpty() ? QString() : ": " + unnamed.join(", "))).toStdString());

        // Each command's description and schema are what a client reads
        std::set<QString> unexplained;
        for (const auto* c : session.registry().commands())
            if (c->description.size() < 8 || c->params["type"] != "object") unexplained.insert(c->name);
        check(unexplained.empty(), "every command describes itself");

        QFile doc(QString(OPENVLSI_SOURCE) + "/docs/api.md");
        const bool readable = doc.open(QIODevice::ReadOnly);
        check(readable, "docs/api.md exists");
        const QString text = QString::fromUtf8(doc.readAll());
        QStringList absent;
        for (const auto* c : session.registry().commands())
            if (!text.contains("### `" + c->name + "`")) absent << c->name;
        check(absent.isEmpty(), ("docs/api.md lists every command (OpenVLSI --dump-api docs/api.md)" + (absent.isEmpty() ? QString() : ": missing " + absent.join(", "))).toStdString());
    }
    QSettings().clear();
    fs::current_path(fs::temp_directory_path());
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
