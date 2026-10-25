#pragma once
// The command layer (docs/rebuild.md): every action of every tool is a command with a name, a description, a JSON
// schema for its parameters and a handler. The GUI, the scripting console and the MCP server all call through
// Registry::call, which validates the parameters (fail closed: an unknown key, a wrong type, a missing required one
// is an error), turns any exception into an error result, and logs every call so a session can be replayed.
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <map>
#include <vector>

namespace oadb { class Workspace; }

namespace cmd {

// What a command acts on: the session's workspace (the open editors and simulation sessions join it later)
struct Context {
    oadb::Workspace& workspace;
};

struct Command {
    QString name;         // [a-z][a-z0-9_]* (MCP tool names allow no dots)
    QString description;  // one sentence: what it does and returns
    QJsonObject params;   // a JSON schema, {"type":"object", "properties":{...}, "required":[...]}
    std::function<QJsonValue(Context&, const QJsonObject&)> run;
};

struct Result {
    bool ok = false;
    QJsonValue value;  // ok: the command's result
    QString error;     // !ok: why
};

class Registry {
public:
    void add(Command c);  // throws std::invalid_argument on a bad name, a duplicate or a malformed schema
    const Command* find(const QString& name) const;
    std::vector<const Command*> commands() const;  // sorted by name
    Result call(Context& ctx, const QString& name, const QJsonObject& args);
    const QJsonArray& log() const { return log_; }  // {"command", "args", "ok", "error"?} per call, in order

private:
    std::map<QString, Command> commands_;
    QJsonArray log_;
};

// value against the schema subset commands use: type (object, string, integer, number, boolean, array; absent: any),
// properties (no others allowed), required, enum, items. Returns "" or the first violation, with its path.
QString validate(const QJsonObject& schema, const QJsonValue& value, const QString& path = "params");

void addCoreCommands(Registry& r);  // workspace, library, cell, view, property, undo/redo, export

}  // namespace cmd
