// test_mcp: the MCP server. The handshake negotiates a version; tools/list describes every command; tools/call
// runs them (a failing command is a tool result with isError, an unknown tool or method a JSON-RPC error);
// notifications get no reply. Then the openvlsi-mcp binary itself, driven over stdio, builds a library on disk.
#include "mcp/server.h"
#include "oadb/oadb.h"

#include <QJsonDocument>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

static int g_fail = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}
static QByteArray req(int id, const char* method, const QJsonObject& params = {}) {
    return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact);
}
static QJsonObject parse(const std::optional<QByteArray>& line) {
    return line ? QJsonDocument::fromJson(*line).object() : QJsonObject{};
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("mcp_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    {   // the protocol, in process
        auto ws = oadb::Workspace::load((dir / "a.libs").string());
        cmd::Registry reg;
        cmd::addCoreCommands(reg);
        cmd::Context ctx{ws};
        mcp::Server s(reg, ctx);

        auto init = parse(s.handle(req(1, "initialize", {{"protocolVersion", "2025-03-26"}, {"capabilities", QJsonObject{}}})));
        auto initNew = parse(s.handle(req(2, "initialize", {{"protocolVersion", "2099-01-01"}})));
        check(init["result"].toObject()["protocolVersion"] == "2025-03-26" &&
                  initNew["result"].toObject()["protocolVersion"] == "2025-06-18" &&
                  init["result"].toObject()["capabilities"].toObject().contains("tools"),
              "initialize echoes a supported version, else offers the newest; it declares tools");
        check(!s.handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"), "a notification gets no reply");

        auto list = parse(s.handle(req(3, "tools/list")))["result"].toObject()["tools"].toArray();
        QJsonObject create;
        for (const auto& t : list) if (t.toObject()["name"] == "cell_create") create = t.toObject();
        check(list.size() == (int)reg.commands().size() && create["inputSchema"].toObject()["required"].toArray().size() == 2,
              "tools/list describes every command with its schema (" + std::to_string(list.size()) + " tools)");

        auto made = parse(s.handle(req(4, "tools/call", {{"name", "library_create"}, {"arguments", QJsonObject{{"library", "lib1"}, {"path", "lib1.oalib"}}}})));
        auto bad = parse(s.handle(req(5, "tools/call", {{"name", "cell_create"}, {"arguments", QJsonObject{{"library", "lib1"}}}})));
        check(!made["result"].toObject()["isError"].toBool() && bad["result"].toObject()["isError"].toBool() &&
                  bad["result"].toObject()["content"].toArray()[0].toObject()["text"].toString().contains("required"),
              "tools/call: a good call succeeds; invalid arguments are a tool error the caller can read");
        auto unknownTool = parse(s.handle(req(6, "tools/call", {{"name", "format_disk"}})));
        auto unknownMethod = parse(s.handle(req(7, "resources/list")));
        auto garbage = parse(s.handle("{not json"));
        check(unknownTool["error"].toObject()["code"] == -32602 && unknownMethod["error"].toObject()["code"] == -32601 &&
                  garbage["error"].toObject()["code"] == -32700,
              "unknown tool, unknown method and unparsable input are JSON-RPC errors");
        check(parse(s.handle(req(8, "ping"))).contains("result"), "ping");
    }

    {   // the binary over stdio: a scripted session builds a library the database then reads
        const fs::path in = dir / "session.jsonl", out = dir / "replies.jsonl", ws = dir / "openvlsi.libs";
        std::ofstream(in) << req(1, "initialize", {{"protocolVersion", "2025-06-18"}}).toStdString() << "\n"
                          << R"({"jsonrpc":"2.0","method":"notifications/initialized"})" << "\n"
                          << req(2, "tools/call", {{"name", "library_create"}, {"arguments", QJsonObject{{"library", "analog"}, {"path", "analog.oalib"}}}}).toStdString() << "\n"
                          << req(3, "tools/call", {{"name", "cell_create"}, {"arguments", QJsonObject{{"library", "analog"}, {"cell", "opamp"}}}}).toStdString() << "\n"
                          << req(4, "tools/call", {{"name", "view_create"}, {"arguments", QJsonObject{{"library", "analog"}, {"cell", "opamp"}, {"view", "schematic"}, {"type", "schematic"}}}}).toStdString() << "\n";
        const std::string run = std::string("'") + OPENVLSI_MCP_EXE + "' --workspace '" + ws.string() + "' < '" + in.string() + "' > '" + out.string() + "'";
        const int rc = std::system(run.c_str());
        std::ifstream replies(out);
        int lines = 0, errors = 0;
        for (std::string l; std::getline(replies, l); ++lines)
            if (QJsonDocument::fromJson(QByteArray::fromStdString(l)).object()["result"].toObject()["isError"].toBool() ||
                QJsonDocument::fromJson(QByteArray::fromStdString(l)).object().contains("error")) ++errors;
        auto w = oadb::Workspace::load(ws.string());
        const auto views = w.library("analog").views("opamp");
        check(rc == 0 && lines == 4 && errors == 0 && views.size() == 1 && views[0].type == "schematic",
              "openvlsi-mcp over stdio: 4 replies (none for the notification), and the library is on disk");
        check(std::system((std::string("'") + OPENVLSI_MCP_EXE + "' 2>/dev/null").c_str()) != 0, "openvlsi-mcp without --workspace is an error");
    }

    fs::remove_all(dir);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
