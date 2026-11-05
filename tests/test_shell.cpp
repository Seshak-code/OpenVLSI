// test_shell: the one window, offscreen. Tools open as tabs, not windows; only the active tool's menus and keys are live;
// the palette finds every command and view; a schema form runs a command and refuses a bad value; the theme is a command.
#include "app/navigator.h"
#include "app/palette.h"
#include "app/shell.h"
#include "app/view_editor.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QSettings>
#include <QTabWidget>
#include <QTreeWidget>

#include <cstdio>
#include <filesystem>
#include <string>
#include <tuple>
#include <unistd.h>

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
    const fs::path dir = fs::temp_directory_path() / ("shell test " + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        app::Theme::apply(qapp, app::Theme::Mode::Dark);
        app::Session session(QString::fromStdString((dir / "openvlsi.libs").string()));
        app::Shell shell(session);
        check(shell.tabTitles() == QStringList{"Start"} && !shell.current(), "a session begins on the start page");

        const auto call = [&](const QString& c, QJsonObject a = {}) { return session.call(c, a); };
        check(call("library_create", {{"library", "analog"}, {"path", QString::fromStdString((dir / "analog.oadb").string())}}).ok, "library_create");
        check(call("cell_create", {{"library", "analog"}, {"cell", "amp"}}).ok && call("cell_create", {{"library", "analog"}, {"cell", "tb"}}).ok, "cell_create");
        for (const auto& [cell, view, type] : {std::tuple{"amp", "schematic", "schematic"}, {"tb", "schematic", "schematic"}, {"tb", "maestro", "maestro"}})
            check(call("view_create", {{"library", "analog"}, {"cell", cell}, {"view", view}, {"type", type}}).ok, std::string("view_create ") + cell + " " + view);

        QTreeWidget* tree = shell.navigator()->tree();
        check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 2, "the navigator follows the database");
        shell.navigator()->setFilter("tb");
        check(tree->topLevelItem(0)->child(0)->isHidden() && !tree->topLevelItem(0)->child(1)->isHidden(), "the filter hides what does not match");
        shell.navigator()->setFilter("");
        check(shell.simulations()->tree()->topLevelItemCount() == 1, "the simulate panel lists the maestro view");

        // Tools open in tabs, inside the one window
        check(call("editor_open", {{"library", "analog"}, {"cell", "amp"}, {"view", "schematic"}}).ok, "editor_open amp");
        check(shell.tabTitles() == QStringList({"Start", "amp schematic"}), "a tool opens as a tab");
        auto* amp = qobject_cast<app::ViewEditor*>(shell.current());
        check(amp && !amp->isWindow(), "the tool is not a window of its own");
        const QStringList ampMenus = shell.menuTitles();
        check(ampMenus.first() == "&File" && ampMenus.contains("&Create") && ampMenus.last() == "&Window", "the active tool's menus join the bar");
        check(call("editor_open", {{"library", "analog"}, {"cell", "tb"}, {"view", "maestro"}}).ok, "editor_open tb maestro");
        check(shell.tabTitles().size() == 3 && !shell.menuTitles().contains("&Create"), "another tool's menus replace them");
        check(call("editor_open", {{"library", "analog"}, {"cell", "amp"}, {"view", "schematic"}}).ok && shell.current() == amp && shell.tabTitles().size() == 3,
              "opening an open view goes to its tab");
        int ctxOk = 0, withKeys = 0;
        for (QAction* a : amp->findChildren<QAction*>())
            if (!a->shortcut().isEmpty()) { ++withKeys; ctxOk += a->shortcutContext() == Qt::WidgetWithChildrenShortcut; }
        check(withKeys > 5 && ctxOk == withKeys, "a tool's keys are scoped to its tab");
        check(shell.recentViews().first() == "analog/tb/maestro" && shell.recentViews().contains("analog/amp/schematic"), "opened views are remembered");

        // The palette: commands and views, best match first
        auto* palette = shell.newPalette();
        check(palette->shown().contains("amp schematic") && palette->shown().contains("cell_create") && palette->shown().contains("ui_theme"), "the palette lists views and commands");
        palette->setQuery("celcre");
        check(palette->shown().value(0) == "cell_create", "it matches a subsequence");
        palette->setQuery("zzzz");
        check(palette->shown().isEmpty(), "and nothing when nothing matches");
        palette->setQuery("tb maestro");
        palette->activateCurrent();
        check(shell.tabs()->tabText(shell.tabs()->currentIndex()) == "tb maestro", "activating a view opens it");
        delete palette;
        check(app::fuzzyScore("cc", "cell_create") > app::fuzzyScore("cc", "xcxxcx") && app::fuzzyScore("q", "abc") < 0, "the score favours word starts");

        // A form is generated from the schema, and refuses what the schema refuses
        const cmd::Command* create = session.registry().find("view_create");
        {
            app::CommandForm form(*create, {{"library", "analog"}, {"cell", "amp"}});
            check(static_cast<QLineEdit*>(form.field("library"))->text() == "analog", "a form prefills from the selection");
            QJsonObject args;
            QString error;
            check(!form.arguments(&args, &error) && error.contains("view"), "a missing required field is refused (" + error.toStdString() + ")");
            static_cast<QLineEdit*>(form.field("view"))->setText("symbol");
            if (auto* t = qobject_cast<QLineEdit*>(form.field("type"))) t->setText("symbol");
            else static_cast<QComboBox*>(form.field("type"))->setCurrentText("symbol");
            check(form.arguments(&args, &error) && args["view"] == "symbol", "a complete form gives its arguments");
        }
        check(!shell.runForm("no_such_command"), "an unknown command does nothing");

        // The theme is a command, and the tokens follow
        check(call("ui_theme", {{"mode", "light"}}).ok && !app::Theme::current().dark && qApp->palette().color(QPalette::Window) == app::Theme::current().window, "ui_theme light");
        check(call("ui_theme", {{"mode", "dark"}}).ok && app::Theme::current().dark, "ui_theme dark");
        check(!call("ui_theme", {{"mode", "sepia"}}).ok, "an unknown theme is refused");
        check(call("ui_tabs").value.toObject()["tabs"].toArray().size() == 3, "ui_tabs");

        // What a typed command reads for itself stays out of the log
        const qsizetype logged = shell.ciw()->logText().size();
        check(shell.ciw()->runLine("ui_theme {\"mode\":\"dark\"}") && !shell.ciw()->logText().mid(logged).contains("cell_list"), "a command's own reads are not echoed");

        // Closing a tab frees the tool; opening it again makes a new one
        const int n = shell.tabs()->count();
        emit shell.tabs()->tabCloseRequested(shell.tabs()->indexOf(amp));
        check(shell.tabs()->count() == n - 1, "closing a tab removes it");
        emit shell.tabs()->tabCloseRequested(0);
        check(shell.tabs()->tabText(0) == "Start" && shell.tabs()->count() == n - 1, "the start page stays");
        qapp.processEvents();
        check(call("editor_open", {{"library", "analog"}, {"cell", "amp"}, {"view", "schematic"}}).ok && shell.tabs()->count() == n, "a closed view opens again");

        check(!shell.ciw()->isWindow() && shell.consoleVisible(), "the command window sits in the shell");
        shell.showConsole(false);
        check(!shell.consoleVisible(), "and can be hidden");
    }
    QSettings().clear();
    fs::remove_all(dir);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
