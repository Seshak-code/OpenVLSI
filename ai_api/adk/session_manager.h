#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QUuid>

namespace AIAPI {

/**
 * ConversationTurn - A single user/AI exchange in a session.
 */
struct ConversationTurn {
    QString role;       // "user" or "assistant"
    QString content;
    QDateTime timestamp;
    QString queryId;    // For linking to feedback

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["role"] = role;
        obj["content"] = content;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        if (!queryId.isEmpty()) obj["queryId"] = queryId;
        return obj;
    }

    static ConversationTurn fromJson(const QJsonObject& obj) {
        ConversationTurn turn;
        turn.role = obj["role"].toString();
        turn.content = obj["content"].toString();
        turn.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        turn.queryId = obj["queryId"].toString();
        return turn;
    }
};

/**
 * SessionState - Persistent session state for an ADK agent interaction.
 */
struct SessionState {
    QString sessionId;
    QString userId;
    QDateTime createdAt;
    QDateTime lastActiveAt;
    QJsonObject circuitState;         // Snapshot of circuit at session start
    QVector<ConversationTurn> history;
    QVector<QString> retrievalLog;    // Chunk IDs retrieved during session

    static constexpr int MAX_HISTORY_TURNS = 200;

    QJsonObject toJson() const;
    static SessionState fromJson(const QJsonObject& obj);
};

/**
 * SessionManager - Singleton lifecycle manager for ADK sessions.
 *
 * Creates, retrieves, and persists sessions. Sessions are stored
 * to ~/.acutesim/sessions/ via SafeIo for crash recovery.
 */
class SessionManager {
public:
    static SessionManager& instance();

    /**
     * Create a new session.
     */
    SessionState createSession(const QString& userId = "local");

    /**
     * Get an existing session by ID.
     */
    SessionState getSession(const QString& sessionId) const;

    /**
     * Check if a session exists.
     */
    bool hasSession(const QString& sessionId) const;

    /**
     * End a session (mark inactive, persist final state).
     */
    void endSession(const QString& sessionId);

    /**
     * Add a conversation turn to a session.
     * Enforces MAX_HISTORY_TURNS via FIFO eviction.
     */
    void addTurn(const QString& sessionId, const ConversationTurn& turn);

    /**
     * Log a retrieval event (chunk IDs used for a query).
     */
    void logRetrieval(const QString& sessionId, const QVector<QString>& chunkIds);

    /**
     * Update the circuit state snapshot for a session.
     */
    void updateCircuitState(const QString& sessionId, const QJsonObject& state);

    /**
     * Get the most recent active session, or empty if none.
     */
    QString getActiveSessionId() const;

    /**
     * Get count of active sessions.
     */
    int activeSessionCount() const;

private:
    SessionManager();

    void saveSession(const SessionState& session);
    SessionState loadSession(const QString& sessionId) const;
    QString getSessionDir() const;
    QString getSessionPath(const QString& sessionId) const;

    mutable QMutex mutex_;
    QMap<QString, SessionState> sessions_;
};

} // namespace AIAPI
