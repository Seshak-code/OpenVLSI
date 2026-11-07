#pragma once
// The running app's API: the same MCP protocol as openvlsi-mcp, one JSON-RPC message per line, on a local socket only this user
// can reach. Calls go through the Session, so every window follows them; the GUI's own commands are there too.
#include "app/session.h"

#include <QObject>
#include <QString>

class QLocalServer;

namespace app {

class ApiServer : public QObject {
    Q_OBJECT
public:
    explicit ApiServer(Session& session, QObject* parent = nullptr);
    ~ApiServer() override;
    QString endpoint() const { return endpoint_; }
    bool listening() const;
    QString error() const { return error_; }

private:
    void accept();
    Session& session_;
    QLocalServer* server_;
    QString endpoint_, error_;
};

}  // namespace app
