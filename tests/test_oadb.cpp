// test_oadb: the design database. Libraries, cells, views and properties persist; every change is one undo step
// that undo/redo replay exactly (a cascade delete included); a failed change leaves nothing behind.
#include "oadb/oadb.h"

#include <sqlite3.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
template <class F> static std::string thrown(F&& f) {
    try { f(); } catch (const oadb::Error& e) { return e.what(); }
    return "";
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("oadb_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "analog.oalib").string();

    std::string before, after;
    {
        auto lib = oadb::Library::create(file, "analog");
        lib->createCell("opamp");
        lib->createView("opamp", "schematic", "schematic");
        lib->createView("opamp", "symbol", "symbol");
        lib->setProp("opamp", "schematic", "instances", R"([{"name":"M1","master":"nmos"}])");
        lib->createCell("bias");
        before = lib->exportText();
        check(lib->cells() == std::vector<std::string>{"bias", "opamp"} && lib->views("opamp").size() == 2,
              "cells and views are listed sorted");

        // one undo step per change; a cascade delete comes back whole
        lib->deleteCell("opamp");
        check(lib->cells() == std::vector<std::string>{"bias"}, "deleting a cell removes it");
        check(lib->undo() && lib->exportText() == before, "undo restores the deleted cell, its views and properties");
        check(lib->redo() && lib->cells() == std::vector<std::string>{"bias"}, "redo deletes it again");
        check(lib->undo() && lib->exportText() == before, "and undo restores it again");

        lib->renameCell("bias", "bias_gen");
        lib->setProp("opamp", "schematic", "instances", R"([])");
        check(lib->undoLabel() == "set opamp/schematic instances", "the undo step is named by its change");
        lib->undo();
        lib->undo();
        check(lib->exportText() == before && lib->canRedo(), "two undos return to the earlier state");
        lib->createCell("ldo");
        check(!lib->canRedo(), "a new change ends the redo history");
        lib->undo();
        check(lib->exportText() == before, "undo of the new change");

        // a change that fails leaves no trace and no undo step
        const std::string label = lib->undoLabel();
        const std::string e = thrown([&] {
            lib->change("two steps", [&] {
                lib->createCell("half");
                lib->createView("nope", "schematic", "schematic");  // no such cell: throws
            });
        });
        check(!e.empty() && lib->exportText() == before && lib->undoLabel() == label,
              "a failed change rolls back whole (" + e + ")");
        check(!thrown([&] { lib->createCell("opamp"); }).empty(), "a duplicate cell is an error");
        check(!thrown([&] { lib->createCell("has space"); }).empty() && !thrown([&] { lib->createCell("a/b"); }).empty(),
              "invalid names are errors");
        lib->setProp("opamp", "schematic", "instances", R"([{"name":"M1","master":"nmos"}])");  // the same value
        check(lib->undoLabel() == label, "setting an unchanged value is no undo step");
        after = lib->exportText();
    }

    // the file keeps it; a reopened library starts with no history
    {
        auto lib = oadb::Library::open(file);
        check(lib->name() == "analog" && lib->exportText() == after && !lib->canUndo(),
              "a reopened library has its contents and no undo history");
    }
    check(!thrown([&] { oadb::Library::create(file, "analog"); }).empty(), "create refuses an existing file");
    {
        std::ofstream(dir / "junk.oalib") << "not a database";
        check(!thrown([&] { oadb::Library::open((dir / "junk.oalib").string()); }).empty(), "open refuses a non-library");
    }

    // a workspace names libraries by path (relative to its file) and round-trips
    {
        auto ws = oadb::Workspace::load((dir / "openvlsi.libs").string());
        ws.defineLibrary("analog", "analog.oalib");
        ws.createLibrary("digital", "digital.oalib");
        fs::create_directories(dir / "with space");
        ws.createLibrary("spaced", "with space/spaced.oalib");
        auto again = oadb::Workspace::load((dir / "openvlsi.libs").string());
        check(again.libraries() == std::vector<std::string>{"analog", "digital", "spaced"} &&
                  again.library("analog").exportText() == after && again.library("spaced").cells().empty(),
              "a saved workspace reopens its libraries (a path with a space too)");
        check(!thrown([&] { again.library("missing"); }).empty(), "an undefined library is an error");
        std::ofstream(dir / "bad.libs") << "DEFINE x\n";
        check(!thrown([&] { oadb::Workspace::load((dir / "bad.libs").string()); }).empty(), "a malformed workspace line is an error");
    }

    // a schema-2 library (no label justification) opens upgraded
    {
        const std::string f = (dir / "old.oalib").string();
        {
            auto l = oadb::Library::create(f, "old");
            l->createCell("c");
            l->createView("c", "symbol", "symbol");
            l->addShape("c", "symbol", {0, "label", "pin", {{0, 0}}, "A", "centerRight"});
        }
        sqlite3* db = nullptr;
        sqlite3_open(f.c_str(), &db);
        const int rc = sqlite3_exec(db, "ALTER TABLE shapes DROP COLUMN justify; UPDATE meta SET value = '2' WHERE key = 'schema'",
                                    nullptr, nullptr, nullptr);
        sqlite3_close(db);
        std::string justify;
        const std::string e = thrown([&] { justify = oadb::Library::open(f)->shapes("c", "symbol").at(0).justify; });
        check(rc == SQLITE_OK && e.empty() && justify == "centerLeft", "a schema-2 library opens, its labels centerLeft (" + e + ")");
        check(!thrown([&] {
                  auto l = oadb::Library::open(f);
                  l->addShape("c", "symbol", {0, "line", "pin", {{0, 0}, {1, 0}}, "", "centerLeft"});
              }).empty(), "justification on a non-label is refused");
    }

    // objects move by an offset; a missing one is an error
    {
        auto l = oadb::Library::create((dir / "move.oalib").string(), "move");
        l->createCell("c");
        l->createView("c", "schematic", "schematic");
        const int64_t w = l->addWire("c", "schematic", {0, {{0, 0}, {10, 0}}});
        l->addPin("c", "schematic", {0, "A", "input", {0, 0}});
        const int64_t p = l->pins("c", "schematic").at(0).id;
        l->moveObject("c", "schematic", "wire", std::to_string(w), 5, -5);
        l->moveObject("c", "schematic", "pin", std::to_string(p), 5, -5);
        check(l->wires("c", "schematic").at(0).points == std::vector<oadb::Point>{{5, -5}, {15, -5}} &&
                  l->pins("c", "schematic").at(0).at == oadb::Point{5, -5},
              "moveObject shifts a wire's points and a pin");
        check(!thrown([&] { l->moveObject("c", "schematic", "label", "42", 1, 1); }).empty() &&
                  !thrown([&] { l->moveObject("c", "schematic", "wire", "x", 1, 1); }).empty(),
              "moving a missing object, or one named by a non-id, is an error");
    }

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
