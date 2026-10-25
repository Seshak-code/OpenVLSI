#pragma once
// A GUI session: the workspace, the command registry, and the one path every window's action takes
// (Session::call → cmd::Registry::call). Windows never write the database themselves: an action the GUI can do is a
// command anything else (a script, an agent through openvlsi-mcp) can do too.
#include "cmd/registry.h"
#include "oadb/oadb.h"

#include <QObject>

#include <memory>

namespace app {

class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(const QString& workspaceFile, QObject* parent = nullptr);
    cmd::Result call(const QString& name, const QJsonObject& args = {});
    const cmd::Registry& registry() const { return registry_; }
    void addCommand(cmd::Command c) { registry_.add(std::move(c)); }  // the GUI's own (editor_open, ...)
    QString workspaceFile() const;

signals:
    void called(const QString& name, const QJsonObject& args, const cmd::Result& result, bool readOnly);  // every call
    void changed();  // a call that writes succeeded: views of the database refresh

private:
    std::unique_ptr<oadb::Workspace> workspace_;
    cmd::Registry registry_;
    std::unique_ptr<cmd::Context> ctx_;
};

}  // namespace app
