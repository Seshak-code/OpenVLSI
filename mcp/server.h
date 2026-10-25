#pragma once
// The MCP server (docs/rebuild.md): the command registry as MCP tools, over MCP's stdio transport (one JSON-RPC 2.0
// message per line). tools/list describes every command (its schema is the tool's inputSchema); tools/call runs one
// through Registry::call, so validation, undo and the call log are the GUI's own. A command that fails is a tool
// result with isError (the model sees why); an unknown tool or method is a JSON-RPC error.
#include "cmd/registry.h"

#include <QByteArray>

#include <optional>

namespace mcp {

class Server {
public:
    Server(cmd::Registry& registry, cmd::Context& ctx) : registry_(registry), ctx_(ctx) {}
    // One incoming line; the response line (no trailing newline), or nothing for a notification
    std::optional<QByteArray> handle(const QByteArray& line);

private:
    cmd::Registry& registry_;
    cmd::Context& ctx_;
};

}  // namespace mcp
