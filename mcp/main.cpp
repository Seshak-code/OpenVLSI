// openvlsi-mcp --workspace <file> [--headless]: the command registry as an MCP server on stdin/stdout (diagnostics on stderr). When
// the app has that workspace open it attaches to the app's API, so the window follows and its own commands are there too; else, or
// with --headless, it works on the database itself and simulates with the treespice beside it.
#include "mcp/endpoint.h"
#include "mcp/server.h"
#include "oadb/oadb.h"

#include <QCoreApplication>
#include <QLocalSocket>

#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // for its executable's directory
    const bool headless = argc == 4 && std::strcmp(argv[3], "--headless") == 0;
    if ((argc != 3 && !headless) || std::strcmp(argv[1], "--workspace") != 0) {
        std::fprintf(stderr, "usage: openvlsi-mcp --workspace <file> [--headless]\n");
        return 2;
    }
    if (!headless) {
        QLocalSocket app;
        app.connectToServer(mcp::endpointFor(QString::fromLocal8Bit(argv[2])));
        if (app.waitForConnected(300)) {  // the app is here: one line each way, a reply for each that has an id
            for (std::string in; std::getline(std::cin, in);) {
                if (in.empty()) continue;
                app.write(in.data(), (qint64)in.size());
                app.write("\n");
                if (!app.waitForBytesWritten(5000)) return 1;
                if (in.find("\"id\"") == std::string::npos) continue;
                while (!app.canReadLine())
                    if (!app.waitForReadyRead(-1)) {
                        std::fprintf(stderr, "openvlsi-mcp: the app went away\n");
                        return 1;
                    }
                const QByteArray line = app.readLine();
                std::cout.write(line.constData(), line.size());
                std::cout << std::flush;
            }
            return 0;
        }
    }
    try {
        auto ws = oadb::Workspace::load(argv[2]);
        cmd::Registry registry;
        cmd::addCoreCommands(registry);
        cmd::addSchematicCommands(registry);
        cmd::addSimulationCommands(registry);
        cmd::addAdeCommands(registry);
        cmd::addPdkCommands(registry);
        cmd::addLayoutCommands(registry);
        cmd::addLayoutEditCommands(registry);
        cmd::addVerifierCommands(registry);
        cmd::addFlowCommands(registry);
        cmd::addTestCommands(registry);
        cmd::addVerifyCommands(registry);
        cmd::addSignoffCommands(registry);
        cmd::addRtlCommands(registry);
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
