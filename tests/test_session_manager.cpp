#include <QtTest>
#ifndef Q_MOC_RUN
#include "session_manager.h"
#endif

class TestSessionManager : public QObject {
    Q_OBJECT

private slots:
    void testCreateSession() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.createSession("test_user");

        QVERIFY(!session.sessionId.isEmpty());
        QCOMPARE(session.userId, QString("test_user"));
        QVERIFY(session.history.isEmpty());
        QVERIFY(mgr.hasSession(session.sessionId));
    }

    void testGetSession() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto created = mgr.createSession("user_get_test");

        auto retrieved = mgr.getSession(created.sessionId);
        QCOMPARE(retrieved.sessionId, created.sessionId);
        QCOMPARE(retrieved.userId, created.userId);
    }

    void testAddTurn() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.createSession("user_turn_test");

        AIAPI::ConversationTurn userTurn;
        userTurn.role = "user";
        userTurn.content = "Add a 10k resistor";
        userTurn.timestamp = QDateTime::currentDateTimeUtc();

        mgr.addTurn(session.sessionId, userTurn);

        auto updated = mgr.getSession(session.sessionId);
        QCOMPARE(updated.history.size(), 1);
        QCOMPARE(updated.history[0].role, QString("user"));
        QCOMPARE(updated.history[0].content, QString("Add a 10k resistor"));
    }

    void testFIFOEviction() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.createSession("user_fifo_test");

        // Add more than MAX_HISTORY_TURNS
        for (int i = 0; i < AIAPI::SessionState::MAX_HISTORY_TURNS + 50; ++i) {
            AIAPI::ConversationTurn turn;
            turn.role = (i % 2 == 0) ? "user" : "assistant";
            turn.content = QString("Message %1").arg(i);
            turn.timestamp = QDateTime::currentDateTimeUtc();
            mgr.addTurn(session.sessionId, turn);
        }

        auto updated = mgr.getSession(session.sessionId);
        QCOMPARE(updated.history.size(), AIAPI::SessionState::MAX_HISTORY_TURNS);

        // First message should be from after eviction started
        // (messages 0-49 should be evicted)
        QCOMPARE(updated.history.first().content, QString("Message 50"));
    }

    void testEndSession() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.createSession("user_end_test");

        QVERIFY(mgr.hasSession(session.sessionId));

        mgr.endSession(session.sessionId);

        QVERIFY(!mgr.hasSession(session.sessionId));
    }

    void testLogRetrieval() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.createSession("user_retrieval_test");

        QVector<QString> chunkIds = {"chunk_1", "chunk_2", "chunk_3"};
        mgr.logRetrieval(session.sessionId, chunkIds);

        auto updated = mgr.getSession(session.sessionId);
        QCOMPARE(updated.retrievalLog.size(), 3);
        QCOMPARE(updated.retrievalLog[0], QString("chunk_1"));
    }

    void testUpdateCircuitState() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.createSession("user_circuit_test");

        QJsonObject state;
        state["componentCount"] = 15;
        state["topology"] = "differential_pair";

        mgr.updateCircuitState(session.sessionId, state);

        auto updated = mgr.getSession(session.sessionId);
        QCOMPARE(updated.circuitState["componentCount"].toInt(), 15);
    }

    void testGetActiveSessionId() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.createSession("user_active_test");

        QString activeId = mgr.getActiveSessionId();
        QVERIFY(!activeId.isEmpty());
        // Active session should be one we know about
        QVERIFY(mgr.hasSession(activeId));
    }

    void testSessionSerialization() {
        AIAPI::SessionState state;
        state.sessionId = "test-session-id";
        state.userId = "test-user";
        state.createdAt = QDateTime::currentDateTimeUtc();
        state.lastActiveAt = state.createdAt;

        AIAPI::ConversationTurn turn;
        turn.role = "user";
        turn.content = "Hello";
        turn.timestamp = QDateTime::currentDateTimeUtc();
        turn.queryId = "q1";
        state.history.append(turn);

        state.retrievalLog.append("chunk_a");

        QJsonObject json = state.toJson();
        AIAPI::SessionState restored = AIAPI::SessionState::fromJson(json);

        QCOMPARE(restored.sessionId, state.sessionId);
        QCOMPARE(restored.userId, state.userId);
        QCOMPARE(restored.history.size(), 1);
        QCOMPARE(restored.history[0].content, QString("Hello"));
        QCOMPARE(restored.retrievalLog.size(), 1);
    }

    void testNonexistentSession() {
        auto& mgr = AIAPI::SessionManager::instance();
        auto session = mgr.getSession("nonexistent-id");
        QVERIFY(session.sessionId.isEmpty());
    }
};

QTEST_MAIN(TestSessionManager)
#include "test_session_manager.moc"
