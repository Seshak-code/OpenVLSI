// test_oadb: the design database. Libraries, cells, views and properties persist; every change is one undo step
// that undo/redo replay exactly (a cascade delete included); a failed change leaves nothing behind.
#include "oadb/oadb.h"

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
        auto again = oadb::Workspace::load((dir / "openvlsi.libs").string());
        check(again.libraries() == std::vector<std::string>{"analog", "digital"} &&
                  again.library("analog").exportText() == after,
              "a saved workspace reopens its libraries");
        check(!thrown([&] { again.library("missing"); }).empty(), "an undefined library is an error");
        std::ofstream(dir / "bad.libs") << "DEFINE x\n";
        check(!thrown([&] { oadb::Workspace::load((dir / "bad.libs").string()); }).empty(), "a malformed workspace line is an error");
    }

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
