#include "storage_manager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/val.h>
#endif

namespace acutesim {
namespace gui {
namespace core {

StorageManager& StorageManager::instance() {
    static StorageManager instance;
    return instance;
}

StorageManager::StorageManager() {
#ifdef __EMSCRIPTEN__
    initIndexedDB();
    // For WASM, use a virtual path that maps to IndexedDB
    baseDir_ = "/acutesim_projects";
#else
    // Desktop: Use platform-specific app data location
    baseDir_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/projects";
    ensureDirectoryExists(baseDir_);
#endif
    qDebug() << "[StorageManager] Initialized with base directory:" << baseDir_;
}

QString StorageManager::getProjectDirectory() const {
    return baseDir_;
}

QString StorageManager::getProjectPath(const QString& projectName) const {
    return baseDir_ + "/" + projectName;
}

QString StorageManager::getFilePath(const QString& projectName, const QString& relativePath) const {
    return getProjectPath(projectName) + "/" + relativePath;
}

void StorageManager::ensureDirectoryExists(const QString& path) const {
#ifndef __EMSCRIPTEN__
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
#endif
}

void StorageManager::setCurrentProject(const QString& projectName) {
    currentProject_ = projectName;
}

// ============================================================================
// Project Operations
// ============================================================================

bool StorageManager::saveProject(const QString& projectName, const QJsonObject& data) {
    QString projectPath = getProjectPath(projectName);
    QString metaPath = projectPath + "/project.json";

#ifdef __EMSCRIPTEN__
    // WASM: Store in IndexedDB via memory cache
    QByteArray jsonData = QJsonDocument(data).toJson(QJsonDocument::Compact);
    memoryCache_[metaPath] = jsonData;

    // Persist to IndexedDB
    EM_ASM({
        var key = UTF8ToString($0);
        var value = UTF8ToString($1);
        try {
            localStorage.setItem(key, value);
        } catch(e) {
            console.error("StorageManager: Failed to save to localStorage:", e);
        }
    }, metaPath.toUtf8().constData(), jsonData.constData());

    emit projectSaved(projectName);
    return true;
#else
    // Desktop: Write to filesystem
    ensureDirectoryExists(projectPath);

    QFile file(metaPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(data).toJson());
        file.close();
        qDebug() << "[StorageManager] Saved project:" << projectName;
        emit projectSaved(projectName);
        return true;
    }

    qWarning() << "[StorageManager] Failed to save project:" << projectName;
    return false;
#endif
}

QJsonObject StorageManager::loadProject(const QString& projectName) {
    QString metaPath = getProjectPath(projectName) + "/project.json";

#ifdef __EMSCRIPTEN__
    // WASM: Load from IndexedDB/localStorage
    char* result = (char*)EM_ASM_PTR({
        var key = UTF8ToString($0);
        var value = localStorage.getItem(key);
        if (value === null) return 0;
        var len = lengthBytesUTF8(value) + 1;
        var ptr = _malloc(len);
        stringToUTF8(value, ptr, len);
        return ptr;
    }, metaPath.toUtf8().constData());

    if (result) {
        QString jsonStr = QString::fromUtf8(result);
        free(result);

        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (doc.isObject()) {
            emit projectLoaded(projectName);
            return doc.object();
        }
    }

    // Fallback to memory cache
    if (memoryCache_.contains(metaPath)) {
        QJsonDocument doc = QJsonDocument::fromJson(memoryCache_[metaPath]);
        if (doc.isObject()) {
            emit projectLoaded(projectName);
            return doc.object();
        }
    }

    return QJsonObject();
#else
    // Desktop: Read from filesystem
    QFile file(metaPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            qDebug() << "[StorageManager] Loaded project:" << projectName;
            emit projectLoaded(projectName);
            return doc.object();
        }
    }

    qDebug() << "[StorageManager] Project not found:" << projectName;
    return QJsonObject();
#endif
}

QStringList StorageManager::listProjects() {
    QStringList projects;

#ifdef __EMSCRIPTEN__
    // WASM: Scan localStorage keys
    int count = EM_ASM_INT({
        return localStorage.length;
    });

    for (int i = 0; i < count; ++i) {
        char* key = (char*)EM_ASM_PTR({
            var key = localStorage.key($0);
            if (key === null) return 0;
            var len = lengthBytesUTF8(key) + 1;
            var ptr = _malloc(len);
            stringToUTF8(key, ptr, len);
            return ptr;
        }, i);

        if (key) {
            QString keyStr = QString::fromUtf8(key);
            free(key);

            // Parse project name from path like "/acutesim_projects/ProjectName/project.json"
            if (keyStr.startsWith(baseDir_) && keyStr.endsWith("/project.json")) {
                QString projectPath = keyStr.mid(baseDir_.length() + 1);
                QString projectName = projectPath.section('/', 0, 0);
                if (!projects.contains(projectName)) {
                    projects.append(projectName);
                }
            }
        }
    }
#else
    // Desktop: List directories
    QDir dir(baseDir_);
    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& entry : entries) {
        QString metaPath = baseDir_ + "/" + entry + "/project.json";
        if (QFile::exists(metaPath)) {
            projects.append(entry);
        }
    }
#endif

    return projects;
}

bool StorageManager::deleteProject(const QString& projectName) {
    QString projectPath = getProjectPath(projectName);

#ifdef __EMSCRIPTEN__
    // WASM: Remove all keys matching this project
    // Note: This is a simplified implementation
    memoryCache_.remove(projectPath + "/project.json");
    EM_ASM({
        var prefix = UTF8ToString($0);
        var keysToRemove = [];
        for (var i = 0; i < localStorage.length; i++) {
            var key = localStorage.key(i);
            if (key && key.startsWith(prefix)) {
                keysToRemove.push(key);
            }
        }
        keysToRemove.forEach(function(key) {
            localStorage.removeItem(key);
        });
    }, projectPath.toUtf8().constData());
    return true;
#else
    // Desktop: Remove directory
    QDir dir(projectPath);
    return dir.removeRecursively();
#endif
}

bool StorageManager::projectExists(const QString& projectName) const {
    QString metaPath = getProjectPath(projectName) + "/project.json";

#ifdef __EMSCRIPTEN__
    int exists = EM_ASM_INT({
        var key = UTF8ToString($0);
        return localStorage.getItem(key) !== null ? 1 : 0;
    }, metaPath.toUtf8().constData());
    return exists != 0 || memoryCache_.contains(metaPath);
#else
    return QFile::exists(metaPath);
#endif
}

// ============================================================================
// File Operations
// ============================================================================

bool StorageManager::saveFile(const QString& projectName, const QString& relativePath, const QByteArray& data) {
    QString filePath = getFilePath(projectName, relativePath);

#ifdef __EMSCRIPTEN__
    // WASM: Store in localStorage
    memoryCache_[filePath] = data;

    EM_ASM({
        var key = UTF8ToString($0);
        var value = UTF8ToString($1);
        try {
            localStorage.setItem(key, value);
        } catch(e) {
            console.error("StorageManager: Failed to save file:", e);
        }
    }, filePath.toUtf8().constData(), data.constData());

    emit fileSaved(projectName, relativePath);
    return true;
#else
    // Desktop: Write to filesystem
    QString dirPath = QFileInfo(filePath).absolutePath();
    ensureDirectoryExists(dirPath);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
        qDebug() << "[StorageManager] Saved file:" << filePath;
        emit fileSaved(projectName, relativePath);
        return true;
    }

    qWarning() << "[StorageManager] Failed to save file:" << filePath;
    return false;
#endif
}

QByteArray StorageManager::loadFile(const QString& projectName, const QString& relativePath) {
    QString filePath = getFilePath(projectName, relativePath);

#ifdef __EMSCRIPTEN__
    // WASM: Load from localStorage
    char* result = (char*)EM_ASM_PTR({
        var key = UTF8ToString($0);
        var value = localStorage.getItem(key);
        if (value === null) return 0;
        var len = lengthBytesUTF8(value) + 1;
        var ptr = _malloc(len);
        stringToUTF8(value, ptr, len);
        return ptr;
    }, filePath.toUtf8().constData());

    if (result) {
        QByteArray data = QByteArray(result);
        free(result);
        emit fileLoaded(projectName, relativePath);
        return data;
    }

    // Fallback to memory cache
    if (memoryCache_.contains(filePath)) {
        emit fileLoaded(projectName, relativePath);
        return memoryCache_[filePath];
    }

    return QByteArray();
#else
    // Desktop: Read from filesystem
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();
        qDebug() << "[StorageManager] Loaded file:" << filePath;
        emit fileLoaded(projectName, relativePath);
        return data;
    }

    qDebug() << "[StorageManager] File not found:" << filePath;
    return QByteArray();
#endif
}

bool StorageManager::fileExists(const QString& projectName, const QString& relativePath) const {
    QString filePath = getFilePath(projectName, relativePath);

#ifdef __EMSCRIPTEN__
    int exists = EM_ASM_INT({
        var key = UTF8ToString($0);
        return localStorage.getItem(key) !== null ? 1 : 0;
    }, filePath.toUtf8().constData());
    return exists != 0 || memoryCache_.contains(filePath);
#else
    return QFile::exists(filePath);
#endif
}

bool StorageManager::deleteFile(const QString& projectName, const QString& relativePath) {
    QString filePath = getFilePath(projectName, relativePath);

#ifdef __EMSCRIPTEN__
    memoryCache_.remove(filePath);
    EM_ASM({
        var key = UTF8ToString($0);
        localStorage.removeItem(key);
    }, filePath.toUtf8().constData());
    return true;
#else
    return QFile::remove(filePath);
#endif
}

QStringList StorageManager::listFiles(const QString& projectName, const QString& category) {
    QStringList files;
    QString projectPath = getProjectPath(projectName);

#ifdef __EMSCRIPTEN__
    QString prefix = projectPath + "/";
    if (!category.isEmpty()) {
        prefix += category + "/";
    }

    int count = EM_ASM_INT({
        return localStorage.length;
    });

    for (int i = 0; i < count; ++i) {
        char* key = (char*)EM_ASM_PTR({
            var key = localStorage.key($0);
            if (key === null) return 0;
            var len = lengthBytesUTF8(key) + 1;
            var ptr = _malloc(len);
            stringToUTF8(key, ptr, len);
            return ptr;
        }, i);

        if (key) {
            QString keyStr = QString::fromUtf8(key);
            free(key);

            if (keyStr.startsWith(prefix) && !keyStr.endsWith("/project.json")) {
                QString relativePath = keyStr.mid(projectPath.length() + 1);
                files.append(relativePath);
            }
        }
    }
#else
    QString searchPath = category.isEmpty() ? projectPath : projectPath + "/" + category;
    QDir dir(searchPath);

    if (dir.exists()) {
        QStringList entries = dir.entryList(QDir::Files);
        for (const QString& entry : entries) {
            if (entry != "project.json") {
                QString relativePath = category.isEmpty() ? entry : category + "/" + entry;
                files.append(relativePath);
            }
        }
    }
#endif

    return files;
}

// Convenience overloads using current project
bool StorageManager::saveFile(const QString& relativePath, const QByteArray& data) {
    if (currentProject_.isEmpty()) {
        qWarning() << "[StorageManager] No current project set";
        return false;
    }
    return saveFile(currentProject_, relativePath, data);
}

QByteArray StorageManager::loadFile(const QString& relativePath) {
    if (currentProject_.isEmpty()) {
        qWarning() << "[StorageManager] No current project set";
        return QByteArray();
    }
    return loadFile(currentProject_, relativePath);
}

bool StorageManager::fileExists(const QString& relativePath) const {
    if (currentProject_.isEmpty()) {
        return false;
    }
    return fileExists(currentProject_, relativePath);
}

#ifdef __EMSCRIPTEN__
void StorageManager::initIndexedDB() {
    // For simplicity, we use localStorage which is synchronous
    // A full IndexedDB implementation would be async
    EM_ASM({
        console.log("StorageManager: Using localStorage for persistence");
    });
    indexedDBReady_ = true;
}
#endif

} // namespace core
} // namespace gui
} // namespace acutesim
