#pragma once
// Where a running app serves its API: a local socket named from its workspace file, so openvlsi-mcp on the same workspace finds it.
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace mcp {

inline QString endpointFor(const QString& workspaceFile) {
    const QByteArray key = QFileInfo(workspaceFile).absoluteFilePath().toUtf8();
    return QDir::tempPath() + "/openvlsi-" + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(16)) + ".sock";
}

}  // namespace mcp
