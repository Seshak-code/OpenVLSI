// test_commands: the command layer. Registration rejects malformed commands; calls validate their parameters (an
// unknown key, a wrong type, a missing one: an error result, never a throw or a default); the core commands drive the
// database end to end, undo and redo included; every call is logged.
#include "cmd/registry.h"
#include "oadb/oadb.h"

#include <QJsonDocument>

#include <cstdio>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
static std::string json(const QJsonValue& v) {
    return QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact).toStdString();
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("cmd_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    cmd::Registry r;
    cmd::addCoreCommands(r);
    auto rejects = [&](cmd::Command c) {
        try { r.add(std::move(c)); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    auto run = [](cmd::Context&, const QJsonObject&) { return QJsonValue(); };
    const QJsonObject empty{{"type", "object"}};
    check(rejects({"cell_create", "dup", empty, run}), "a duplicate command name is rejected");
    check(rejects({"Bad.Name", "x", empty, run}), "a name outside [a-z][a-z0-9_]* is rejected");
    check(rejects({"no_schema", "x", QJsonObject{{"type", "string"}}, run}), "params must be an object schema");
    check(rejects({"odd_schema", "x", QJsonObject{{"type", "object"}, {"minLength", 3}}, run}),
          "an unsupported schema keyword is rejected at registration");

    auto ws = oadb::Workspace::load((dir / "openvlsi.libs").string());
    cmd::Context ctx{ws};
    auto call = [&](const char* name, const QJsonObject& a) { return r.call(ctx, name, a); };

    // validation
    auto e1 = call("cell_create", {{"library", "analog"}});
    auto e2 = call("cell_create", {{"library", "analog"}, {"cell", 3}});
    auto e3 = call("cell_create", {{"library", "analog"}, {"cell", "x"}, {"colour", "red"}});
    auto e4 = call("no_such_command", {});
    check(!e1.ok && e1.error.contains("cell: required"), "a missing parameter is an error (" + e1.error.toStdString() + ")");
    check(!e2.ok && e2.error.contains("expected string"), "a wrong type is an error (" + e2.error.toStdString() + ")");
    check(!e3.ok && e3.error.contains("unknown parameter"), "an unknown parameter is an error (" + e3.error.toStdString() + ")");
    check(!e4.ok, "an unknown command is an error");

    // the Library Manager's flow, through commands
    check(call("library_create", {{"library", "analog"}, {"path", "analog.oalib"}}).ok, "library_create");
    check(call("cell_create", {{"library", "analog"}, {"cell", "opamp"}}).ok &&
              call("view_create", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"type", "schematic"}}).ok,
          "cell_create and view_create");
    const QJsonObject inst{{"name", "M1"}, {"master", "nmos"}, {"w", 1e-6}};
    check(call("prop_set", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"key", "m1"}, {"value", inst}}).ok &&
              call("prop_set", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"key", "gain"}, {"value", 42.5}}).ok,
          "prop_set takes objects and scalars");
    auto g1 = call("prop_get", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"key", "m1"}});
    auto g2 = call("prop_get", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"key", "gain"}});
    check(g1.ok && g1.value.toObject() == inst && g2.ok && g2.value.toDouble() == 42.5,
          "prop_get returns what was set (" + json(g1.value) + ", " + json(g2.value) + ")");
    auto missing = call("prop_get", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"key", "nope"}});
    check(!missing.ok, "a missing property is an error, not null");

    auto u = call("edit_undo", {{"library", "analog"}});
    auto g3 = call("prop_get", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"key", "gain"}});
    check(u.ok && u.value.toString() == "set opamp/schematic gain" && !g3.ok, "edit_undo undoes the last change (" + u.value.toString().toStdString() + ")");
    check(call("edit_redo", {{"library", "analog"}}).ok &&
              call("prop_get", {{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"key", "gain"}}).value.toDouble() == 42.5,
          "edit_redo redoes it");
    check(!call("edit_redo", {{"library", "analog"}}).ok, "nothing to redo is an error");
    check(!call("cell_create", {{"library", "missing"}, {"cell", "x"}}).ok, "an undefined library is an error result, not a crash");

    auto ex = call("library_export", {{"library", "analog"}});
    check(ex.ok && ex.value.toString().contains("cell opamp") && ex.value.toString().contains("gain = 42.5"),
          "library_export returns the library as text");
    auto views = call("view_list", {{"library", "analog"}, {"cell", "opamp"}});
    check(views.ok && views.value.toArray().size() == 1 && views.value.toArray()[0].toObject()["type"] == "schematic", "view_list");

    // the log: every call, in order, with its outcome
    const QJsonArray log = r.log();
    check(log.size() > 10 && log[0].toObject()["command"] == "cell_create" && !log[0].toObject()["ok"].toBool() &&
              log.last().toObject()["command"] == "view_list" && log.last().toObject()["ok"].toBool(),
          "every call is logged in order with its outcome (" + std::to_string(log.size()) + " entries)");

    // the registry describes itself: what the MCP server lists
    bool described = true;
    for (const auto* c : r.commands()) described = described && !c->description.isEmpty() && c->params["type"] == "object";
    check(described && r.commands().size() >= 15, "every command has a description and an object schema (" +
                                                       std::to_string(r.commands().size()) + " commands)");

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
