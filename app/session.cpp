#include "app/session.h"

namespace app {

Session::Session(const QString& workspaceFile, QObject* parent)
    : QObject(parent), workspace_(std::make_unique<oadb::Workspace>(oadb::Workspace::load(workspaceFile.toStdString()))) {
    cmd::addCoreCommands(registry_);
    cmd::addSchematicCommands(registry_);
    ctx_ = std::make_unique<cmd::Context>(cmd::Context{*workspace_});
}

QString Session::workspaceFile() const { return QString::fromStdString(workspace_->file()); }

cmd::Result Session::call(const QString& name, const QJsonObject& args) {
    const cmd::Result r = registry_.call(*ctx_, name, args);
    const cmd::Command* c = registry_.find(name);
    const bool readOnly = c && c->readOnly;
    emit called(name, args, r, readOnly);
    if (r.ok && !readOnly) emit changed();
    return r;
}

}  // namespace app
