#include "app/api_server.h"

#include "mcp/endpoint.h"
#include "mcp/server.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>

#include <memory>

namespace app {

ApiServer::ApiServer(Session& session, QObject* parent) : QObject(parent), session_(session), server_(new QLocalServer(this)) {
    endpoint_ = mcp::endpointFor(session_.workspaceFile());
    server_->setSocketOptions(QLocalServer::UserAccessOption);  // this user only: it can do anything the user can
    // A socket file left by a crash: another app answering it means this workspace is already open there
    QLocalSocket probe;
    probe.connectToServer(endpoint_);
    if (probe.waitForConnected(200)) {
        probe.disconnectFromServer();
        error_ = "another OpenVLSI serves this workspace at " + endpoint_;
        return;
    }
    QLocalServer::removeServer(endpoint_);
    if (!server_->listen(endpoint_)) {
        error_ = server_->errorString();
        return;
    }
    connect(server_, &QLocalServer::newConnection, this, [this] { accept(); });
}

ApiServer::~ApiServer() = default;

bool ApiServer::listening() const { return server_->isListening(); }

void ApiServer::accept() {
    while (QLocalSocket* socket = server_->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        auto protocol = std::make_shared<mcp::Server>(session_.registry(), [this](const QString& name, const QJsonObject& args) { return session_.call(name, args); });
        // ponytail: a call runs on the GUI thread, so a client's flow_run with wait true freezes the window for its length; use
        // wait false, or a worker thread when commands are made safe off it
        connect(socket, &QLocalSocket::readyRead, socket, [socket, protocol] {
            while (socket->canReadLine()) {
                const QByteArray line = socket->readLine().trimmed();
                if (line.isEmpty()) continue;
                if (const auto out = protocol->handle(line)) {
                    socket->write(*out);
                    socket->write("\n");
                    socket->flush();
                }
            }
        });
    }
}

}  // namespace app
