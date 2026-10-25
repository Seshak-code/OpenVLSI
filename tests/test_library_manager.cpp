// test_library_manager: the app shell, offscreen. The Library Manager's actions run commands through the session and
// its columns follow the database (an undo, a CIW line, another process's write after refresh); reads neither refresh
// the windows nor fill the CIW unless typed there; the CIW runs `command {json}` and reports what fails.
#include "app/ciw.h"
#include "app/library_manager.h"
#include "app/view_editor.h"

#include <QApplication>

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("lm test " + std::to_string(::getpid()));  // a space on purpose
    fs::remove_all(dir);
    fs::create_directories(dir);
    const QString ws = QString::fromStdString((dir / "openvlsi.libs").string());
    {
        app::Session session(ws);
        int changes = 0;
        QObject::connect(&session, &app::Session::changed, [&] { ++changes; });

        std::set<std::string> reads;
        for (const auto* c : session.registry().commands()) if (c->readOnly) reads.insert(c->name.toStdString());
        check(reads == std::set<std::string>{"workspace_libraries", "library_export", "cell_list", "view_list", "prop_get",
                                             "cell_prop_get", "view_get", "schematic_check",
                                             "netlist", "simulate", "results_get", "wave_eval", "ade_setup_get", "ade_run", "ade_stop",
                                             "ade_history_list", "ade_history_get"},
              "exactly the commands that only read are marked read-only");

        app::Ciw ciw(session);
        app::LibraryManager& lm = *ciw.libraryManager();
        check(lm.libraries().isEmpty() && changes == 0, "an empty workspace: no libraries, and browsing changes nothing");

        check(lm.createLibrary("analog", QString::fromStdString((dir / "analog.oalib").string())) &&
                  lm.libraries() == QStringList{"analog"} && lm.selectedLibrary() == "analog",
              "New Library creates, lists and selects it");
        check(lm.createCell("inv") && lm.cells() == QStringList{"inv"} && lm.selectedCell() == "inv", "New Cell");
        check(lm.createView("schematic", "schematic") && lm.createView("symbol", "symbol") &&
                  lm.views() == QStringList{"schematic (schematic)", "symbol (symbol)"} && lm.selectedView() == "symbol",
              "New View lists each view with its type and selects the new one");
        check(!lm.createCell("inv") && lm.cells() == QStringList{"inv"}, "a duplicate cell fails and changes nothing");
        check(changes == 4, "each successful write refreshes once (" + std::to_string(changes) + "); reads and failures do not");

        check(lm.deleteSelected() && lm.views() == QStringList{"schematic (schematic)"}, "Delete removes the selected view");
        check(lm.undo() && lm.views() == QStringList{"schematic (schematic)", "symbol (symbol)"}, "Undo brings it back");
        lm.select("analog", "inv");
        check(lm.renameCell("inverter") && lm.cells() == QStringList{"inverter"} && lm.selectedCell() == "inverter",
              "Rename keeps the cell selected under its new name");
        check(lm.deleteSelected() && lm.cells().isEmpty() && lm.undo() && lm.cells() == QStringList{"inverter"},
              "Delete with no view selected removes the cell; Undo restores it");

        oadb::Library::create((dir / "digital.oalib").string(), "digital");
        check(lm.defineLibrary("digital", QString::fromStdString((dir / "digital.oalib").string())) &&
                  lm.libraries() == QStringList{"analog", "digital"} && lm.selectedLibrary() == "digital" && lm.cells().isEmpty(),
              "Define Library adds an existing library file");

        const QString before = ciw.logText();
        check(before.contains("cell_create") && before.contains("cell_rename") && !before.contains("cell_list"),
              "the CIW echoes the windows' changes, not their browsing");
        check(ciw.runLine(R"(cell_create {"library": "digital", "cell": "nand2"})") && lm.cells() == QStringList{"nand2"},
              "a CIW line runs a command and the Library Manager follows");
        check(ciw.runLine(R"(cell_list {"library": "digital"})") && ciw.logText().contains("cell_list") &&
                  ciw.logText().contains("[\"nand2\"]"),
              "a typed read is echoed with its result");
        check(!ciw.runLine(R"(cell_list {"library": )") && ciw.logText().contains("not a JSON object"), "malformed JSON is reported");
        check(!ciw.runLine("format_disk") && ciw.logText().contains("format_disk"), "an unknown command is reported");
        check(!ciw.runLine(R"(cell_list {"library": "digital", "extra": 1})"), "invalid arguments fail");

        lm.select("analog", "inverter", "schematic");
        check(lm.openSelected() && ciw.editor("analog", "inverter", "schematic"), "opening a view (a double-click) opens its editor");

        {
            auto other = oadb::Workspace::load(ws.toStdString());  // another process writing the same library
            other.library("digital").createCell("nor2");
        }
        lm.select("digital");
        lm.refresh();
        check(lm.cells() == QStringList{"nand2", "nor2"}, "refresh shows another writer's cells");
    }
    {
        app::Session again(ws);
        check(again.call("workspace_libraries").value.toArray().size() == 2, "the workspace file was saved (both libraries)");
    }

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
