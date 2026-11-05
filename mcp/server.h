#pragma once
// The MCP server (docs/rebuild.md): the command registry as MCP tools, over MCP's stdio transport (one JSON-RPC 2.0
// message per line). tools/list describes every command (its schema is the tool's inputSchema); tools/call runs one
// through Registry::call, so validation, undo and the call log are the GUI's own. A command that fails is a tool
// result with isError (the model sees why); an unknown tool or method is a JSON-RPC error.
#include "cmd/registry.h"

#include <QByteArray>

#include <functional>
#include <optional>

namespace mcp {

class Server {
public:
    using Caller = std::function<cmd::Result(const QString&, const QJsonObject&)>;
    Server(cmd::Registry& registry, cmd::Context& ctx) : registry_(registry), call_([&registry, &ctx](const QString& n, const QJsonObject& a) { return registry.call(ctx, n, a); }) {}
    Server(const cmd::Registry& registry, Caller call) : registry_(registry), call_(std::move(call)) {}  // the app's: through its Session
    // One incoming line; the response line (no trailing newline), or nothing for a notification
    std::optional<QByteArray> handle(const QByteArray& line);

private:
    const cmd::Registry& registry_;
    Caller call_;
};

}  // namespace mcp
