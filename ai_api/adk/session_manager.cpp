#include "session_manager.h"
#include "../context/safe_io.h"
#include <QDir>
#include <QStandardPaths>

namespace AIAPI {

// ============================================================================
// SessionState Serialization
// ============================================================================

QJsonObject SessionState::toJson() const {
    QJsonObject obj;
    obj["sessionId"] = sessionId;
    obj["userId"] = userId;
    obj["createdAt"] = createdAt.toString(Qt::ISODate);
    obj["lastActiveAt"] = lastActiveAt.toString(Qt::ISODate);
    obj["circuitState"] = circuitState;

    QJsonArray historyArr;
    for (const auto& turn : history) {
        historyArr.append(turn.toJson());
    }
    obj["history"] = historyArr;

    QJsonArray retrievalArr;
    for (const auto& id : retrievalLog) {
        retrievalArr.append(id);
    }
    obj["retrievalLog"] = retrievalArr;

    return obj;
}

SessionState SessionState::fromJson(const QJsonObject& obj) {
    SessionState state;
    state.sessionId = obj["sessionId"].toString();
    state.userId = obj["userId"].toString();
    state.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
    state.lastActiveAt = QDateTime::fromString(obj["lastActiveAt"].toString(), Qt::ISODate);
    state.circuitState = obj["circuitState"].toObject();

    QJsonArray historyArr = obj["history"].toArray();
    for (const auto& val : historyArr) {
        state.history.append(ConversationTurn::fromJson(val.toObject()));
    }

    QJsonArray retrievalArr = obj["retrievalLog"].toArray();
    for (const auto& val : retrievalArr) {
        state.retrievalLog.append(val.toString());
    }

    return state;
}

// ============================================================================
// SessionManager Singleton
// ============================================================================

SessionManager::SessionManager() {
    // Load any persisted sessions from disk
    QString dir = getSessionDir();
    QDir sessionDir(dir);
    if (sessionDir.exists()) {
        QStringList files = sessionDir.entryList({"*.json"}, QDir::Files);
        for (const auto& file : files) {
            QString sessionId = file.left(file.size() - 5); // Strip .json
            SessionState loaded = loadSession(sessionId);
            if (!loaded.sessionId.isEmpty()) {
                sessions_[loaded.sessionId] = loaded;
            }
        }
    }
}

SessionManager& SessionManager::instance() {
    static SessionManager _instance;
    return _instance;
}

SessionState SessionManager::createSession(const QString& userId) {
    QMutexLocker locker(&mutex_);

    SessionState session;
    session.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.userId = userId;
    session.createdAt = QDateTime::currentDateTimeUtc();
    session.lastActiveAt = session.createdAt;

    sessions_[session.sessionId] = session;
    saveSession(session);

    return session;
}

SessionState SessionManager::getSession(const QString& sessionId) const {
    QMutexLocker locker(&mutex_);
    return sessions_.value(sessionId);
}

bool SessionManager::hasSession(const QString& sessionId) const {
    QMutexLocker locker(&mutex_);
    return sessions_.contains(sessionId);
}

void SessionManager::endSession(const QString& sessionId) {
    QMutexLocker locker(&mutex_);

    if (!sessions_.contains(sessionId)) return;

    auto& session = sessions_[sessionId];
    session.lastActiveAt = QDateTime::currentDateTimeUtc();
    saveSession(session);
    sessions_.remove(sessionId);
}

void SessionManager::addTurn(const QString& sessionId, const ConversationTurn& turn) {
    QMutexLocker locker(&mutex_);

    if (!sessions_.contains(sessionId)) return;

    auto& session = sessions_[sessionId];
    session.history.append(turn);

    // FIFO eviction to enforce bounded history
    while (session.history.size() > SessionState::MAX_HISTORY_TURNS) {
        session.history.removeFirst();
    }

    session.lastActiveAt = QDateTime::currentDateTimeUtc();
    saveSession(session);
}

void SessionManager::logRetrieval(const QString& sessionId, const QVector<QString>& chunkIds) {
    QMutexLocker locker(&mutex_);

    if (!sessions_.contains(sessionId)) return;

    auto& session = sessions_[sessionId];
    for (const auto& id : chunkIds) {
        session.retrievalLog.append(id);
    }
    session.lastActiveAt = QDateTime::currentDateTimeUtc();
}

void SessionManager::updateCircuitState(const QString& sessionId, const QJsonObject& state) {
    QMutexLocker locker(&mutex_);

    if (!sessions_.contains(sessionId)) return;

    sessions_[sessionId].circuitState = state;
    sessions_[sessionId].lastActiveAt = QDateTime::currentDateTimeUtc();
}

QString SessionManager::getActiveSessionId() const {
    QMutexLocker locker(&mutex_);

    if (sessions_.isEmpty()) return {};

    // Return most recently active session
    QString bestId;
    QDateTime bestTime;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (bestId.isEmpty() || it.value().lastActiveAt > bestTime) {
            bestId = it.key();
            bestTime = it.value().lastActiveAt;
        }
    }
    return bestId;
}

int SessionManager::activeSessionCount() const {
    QMutexLocker locker(&mutex_);
    return sessions_.size();
}

// ============================================================================
// Persistence
// ============================================================================

void SessionManager::saveSession(const SessionState& session) {
    QString path = getSessionPath(session.sessionId);
    QDir().mkpath(getSessionDir());
    SafeIo::write_json_safe(path, session.toJson(), false);
}

SessionState SessionManager::loadSession(const QString& sessionId) const {
    QString path = getSessionPath(sessionId);
    auto result = SafeIo::read_json_file_bounded(path, SafeIo::MAX_AI_CONTEXT_SIZE);
    if (result.success && result.isObject()) {
        return SessionState::fromJson(result.toObject());
    }
    return {};
}

QString SessionManager::getSessionDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/sessions";
}

QString SessionManager::getSessionPath(const QString& sessionId) const {
    return getSessionDir() + "/" + sessionId + ".json";
}

} // namespace AIAPI
