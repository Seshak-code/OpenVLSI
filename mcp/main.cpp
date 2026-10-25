// openvlsi-mcp --workspace <file>: the command registry as an MCP server on stdin/stdout (diagnostics on stderr). It
// simulates with the treespice beside it.
#include "mcp/server.h"
#include "oadb/oadb.h"

#include <QCoreApplication>

#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // for its executable's directory
    if (argc != 3 || std::strcmp(argv[1], "--workspace") != 0) {
        std::fprintf(stderr, "usage: openvlsi-mcp --workspace <file>\n");
        return 2;
    }
    try {
        auto ws = oadb::Workspace::load(argv[2]);
        cmd::Registry registry;
        cmd::addCoreCommands(registry);
        cmd::addSchematicCommands(registry);
        cmd::addSimulationCommands(registry);
        cmd::Context ctx{ws, (QCoreApplication::applicationDirPath() + "/treespice").toStdString()};
        mcp::Server server(registry, ctx);
        for (std::string in; std::getline(std::cin, in);) {
            if (in.empty()) continue;
            if (const auto out = server.handle(QByteArray::fromStdString(in))) {
                std::cout.write(out->constData(), out->size());
                std::cout << '\n' << std::flush;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "openvlsi-mcp: %s\n", e.what());
        return 1;
    }
    return 0;
}
