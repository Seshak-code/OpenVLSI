#pragma once
/**
 * safe_io.h
 * 
 * NASA/JPL-Style Safe I/O Utilities for High-Reliability Code.
 * Provides bounded file reading and atomic writing to prevent
 * corruption, overflow, and denial-of-service via malformed inputs.
 */

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QString>
#include <QDebug>

namespace SafeIo {

// =============================================================================
// CONFIGURATION
// =============================================================================

/**
 * Data Size Tiers (NASA-style resource limits)
 */
constexpr qint64 MAX_CONFIG_SIZE            = 1 * 1024 * 1024;   // 1 MB: User preferences, simple configs
constexpr qint64 MAX_AI_CONTEXT_SIZE        = 10 * 1024 * 1024;  // 10 MB: Chat history, feedback logs
constexpr qint64 MAX_RAG_DATA_SIZE          = 50 * 1024 * 1024;  // 50 MB: Knowledge base, reference designs
constexpr qint64 MAX_SIMULATION_TRACE_SIZE  = 250 * 1024 * 1024; // 250 MB: Raw simulation output (binary/JSON)

/** Default safety limit if none specified */
constexpr qint64 DEFAULT_MAX_FILE_SIZE = MAX_AI_CONTEXT_SIZE;

/** Maximum entries for history-type arrays */
constexpr int MAX_HISTORY_ENTRIES = 1000;

// =============================================================================
// RESULT TYPES
// =============================================================================

/**
 * ReadResult - Result of a bounded file read operation.
 */
struct ReadResult {
    bool success = false;
    QString errorMessage;
    QByteArray data;
};

/**
 * JsonReadResult - Result of a bounded JSON file read.
 */
struct JsonReadResult {
    bool success = false;
    QString errorMessage;
    QJsonDocument document;
    
    QJsonObject toObject() const { return document.object(); }
    QJsonArray toArray() const { return document.array(); }
    bool isArray() const { return document.isArray(); }
    bool isObject() const { return document.isObject(); }
};

/**
 * WriteResult - Result of a safe file write operation.
 */
struct WriteResult {
    bool success = false;
    QString errorMessage;
};

// =============================================================================
// BOUNDED READING
// =============================================================================

/**
 * read_file_bounded - Read file with strict size limit.
 * 
 * @param path      Absolute path to file.
 * @param maxSize   Maximum allowed file size (default: MAX_JSON_FILE_SIZE).
 * @return ReadResult with data or error.
 */
inline ReadResult read_file_bounded(const QString& path, 
                                     qint64 maxSize = DEFAULT_MAX_FILE_SIZE) {
    ReadResult result;
    
    QFile file(path);
    
    // Check existence
    if (!file.exists()) {
        result.errorMessage = QString("File does not exist: %1").arg(path);
        qWarning() << "[SafeIo]" << result.errorMessage;
        return result;
    }
    
    // Check size before opening
    if (file.size() > maxSize) {
        result.errorMessage = QString("File exceeds size limit (%1 > %2 bytes): %3")
                              .arg(file.size()).arg(maxSize).arg(path);
        qWarning() << "[SafeIo]" << result.errorMessage;
        return result;
    }
    
    // Open and read
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QString("Cannot open file: %1 (%2)")
                              .arg(path).arg(file.errorString());
        qWarning() << "[SafeIo]" << result.errorMessage;
        return result;
    }
    
    result.data = file.readAll();
    file.close();
    
    result.success = true;
    return result;
}

/**
 * read_json_file_bounded - Read and parse JSON with size and validity checks.
 * 
 * @param path      Absolute path to JSON file.
 * @param maxSize   Maximum allowed file size.
 * @return JsonReadResult with parsed document or error.
 */
inline JsonReadResult read_json_file_bounded(const QString& path,
                                              qint64 maxSize = DEFAULT_MAX_FILE_SIZE) {
    JsonReadResult result;
    
    // Read raw bytes with size check
    ReadResult raw = read_file_bounded(path, maxSize);
    if (!raw.success) {
        result.errorMessage = raw.errorMessage;
        return result;
    }
    
    // Parse JSON
    QJsonParseError parseError;
    result.document = QJsonDocument::fromJson(raw.data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        result.errorMessage = QString("JSON parse error at offset %1: %2")
                              .arg(parseError.offset)
                              .arg(parseError.errorString());
        qWarning() << "[SafeIo]" << result.errorMessage << "in" << path;
        return result;
    }
    
    result.success = true;
    return result;
}

// =============================================================================
// ATOMIC WRITING
// =============================================================================

/**
 * write_json_safe - Atomically write JSON to file.
 * 
 * Uses QSaveFile for atomic write (write to temp, then rename).
 * This prevents corruption if the process crashes during write.
 * 
 * @param path      Absolute path to target file.
 * @param document  JSON document to write.
 * @param compact   If true, use compact format; otherwise indented.
 * @return WriteResult indicating success or error.
 */
inline WriteResult write_json_safe(const QString& path,
                                    const QJsonDocument& document,
                                    bool compact = true) {
    WriteResult result;
    
    QSaveFile file(path);
    
    if (!file.open(QIODevice::WriteOnly)) {
        result.errorMessage = QString("Cannot open file for writing: %1 (%2)")
                              .arg(path).arg(file.errorString());
        qWarning() << "[SafeIo]" << result.errorMessage;
        return result;
    }
    
    QJsonDocument::JsonFormat format = compact 
        ? QJsonDocument::Compact 
        : QJsonDocument::Indented;
    
    QByteArray data = document.toJson(format);
    
    if (file.write(data) != data.size()) {
        result.errorMessage = QString("Write incomplete: %1").arg(path);
        qWarning() << "[SafeIo]" << result.errorMessage;
        file.cancelWriting();
        return result;
    }
    
    if (!file.commit()) {
        result.errorMessage = QString("Atomic commit failed: %1 (%2)")
                              .arg(path).arg(file.errorString());
        qWarning() << "[SafeIo]" << result.errorMessage;
        return result;
    }
    
    result.success = true;
    return result;
}

/**
 * write_json_safe - Convenience overload for QJsonObject.
 */
inline WriteResult write_json_safe(const QString& path,
                                    const QJsonObject& object,
                                    bool compact = true) {
    return write_json_safe(path, QJsonDocument(object), compact);
}

/**
 * write_json_safe - Convenience overload for QJsonArray.
 */
inline WriteResult write_json_safe(const QString& path,
                                    const QJsonArray& array,
                                    bool compact = true) {
    return write_json_safe(path, QJsonDocument(array), compact);
}

} // namespace SafeIo
