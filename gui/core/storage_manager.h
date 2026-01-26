#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QStringList>

namespace acutesim {
namespace gui {
namespace core {

/**
 * StorageManager - Cross-platform file persistence layer.
 *
 * Desktop: Uses QStandardPaths::AppDataLocation for filesystem storage
 * WASM: Uses IndexedDB via Emscripten for browser persistence
 *
 * This singleton provides unified project and file storage that works
 * seamlessly across desktop and web deployments.
 */
class StorageManager : public QObject {
    Q_OBJECT

public:
    static StorageManager& instance();

    // Project directory (platform-specific)
    QString getProjectDirectory() const;

    // Project operations
    bool saveProject(const QString& projectName, const QJsonObject& data);
    QJsonObject loadProject(const QString& projectName);
    QStringList listProjects();
    bool deleteProject(const QString& projectName);
    bool projectExists(const QString& projectName) const;

    // File operations within a project
    bool saveFile(const QString& projectName, const QString& relativePath, const QByteArray& data);
    QByteArray loadFile(const QString& projectName, const QString& relativePath);
    bool fileExists(const QString& projectName, const QString& relativePath) const;
    bool deleteFile(const QString& projectName, const QString& relativePath);
    QStringList listFiles(const QString& projectName, const QString& category = QString());

    // Convenience overloads for current project
    void setCurrentProject(const QString& projectName);
    QString currentProject() const { return currentProject_; }
    bool saveFile(const QString& relativePath, const QByteArray& data);
    QByteArray loadFile(const QString& relativePath);
    bool fileExists(const QString& relativePath) const;

signals:
    void projectSaved(const QString& name);
    void projectLoaded(const QString& name);
    void fileSaved(const QString& projectName, const QString& relativePath);
    void fileLoaded(const QString& projectName, const QString& relativePath);

private:
    StorageManager();
    ~StorageManager() override = default;
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    QString getProjectPath(const QString& projectName) const;
    QString getFilePath(const QString& projectName, const QString& relativePath) const;
    void ensureDirectoryExists(const QString& path) const;

    QString baseDir_;
    QString currentProject_;

#ifdef __EMSCRIPTEN__
    // WASM-specific: IndexedDB initialization
    void initIndexedDB();
    bool indexedDBReady_ = false;

    // Store data in memory as fallback/cache for IndexedDB
    QMap<QString, QByteArray> memoryCache_;
#endif
};

} // namespace core
} // namespace gui
} // namespace acutesim
