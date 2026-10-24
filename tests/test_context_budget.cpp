#include <QTest>
#ifndef Q_MOC_RUN
#include "../ai_api/context/context_budget.h"
#endif

class TestContextBudget : public QObject {
    Q_OBJECT

private slots:
    void testTokenEstimation() {
        QCOMPARE(AIAPI::ContextBudgetManager::estimateTokens(""), 0);

        int single = AIAPI::ContextBudgetManager::estimateTokens("hello");
        QVERIFY(single >= 1);

        int sentence = AIAPI::ContextBudgetManager::estimateTokens("the quick brown fox jumps");
        QVERIFY(sentence >= 5);
        QVERIFY(sentence <= 15);

        int longer = AIAPI::ContextBudgetManager::estimateTokens(
            "This is a longer piece of text with many words to test scaling behavior");
        QVERIFY(longer > sentence);
    }

    void testPriorityAssignment() {
        AIAPI::TraceChunk failureChunk;
        failureChunk.contentType = "FailureLog";
        QCOMPARE(AIAPI::ContextBudgetManager::assignPriority(failureChunk), AIAPI::ChunkPriority::CRITICAL);

        AIAPI::TraceChunk debugChunk;
        debugChunk.contentType = "DebugContext";
        QCOMPARE(AIAPI::ContextBudgetManager::assignPriority(debugChunk), AIAPI::ChunkPriority::CRITICAL);

        AIAPI::TraceChunk signalChunk;
        signalChunk.contentType = "SignalPath";
        QCOMPARE(AIAPI::ContextBudgetManager::assignPriority(signalChunk), AIAPI::ChunkPriority::HIGH);

        AIAPI::TraceChunk summaryChunk;
        summaryChunk.contentType = "Summary";
        QCOMPARE(AIAPI::ContextBudgetManager::assignPriority(summaryChunk), AIAPI::ChunkPriority::MEDIUM);

        AIAPI::TraceChunk waveformChunk;
        waveformChunk.contentType = "WaveformSummary";
        QCOMPARE(AIAPI::ContextBudgetManager::assignPriority(waveformChunk), AIAPI::ChunkPriority::LOW);

        AIAPI::TraceChunk refChunk;
        refChunk.contentType = "ReferenceDesign";
        QCOMPARE(AIAPI::ContextBudgetManager::assignPriority(refChunk), AIAPI::ChunkPriority::EXTRA);
    }

    void testBudgetAllocationStrictMode() {
        QVector<AIAPI::BudgetedChunk> candidates;

        AIAPI::BudgetedChunk critical;
        critical.chunkId = "c1";
        critical.content = "Failure detected at node X";
        critical.contentType = "FailureLog";
        critical.priority = AIAPI::ChunkPriority::CRITICAL;
        critical.relevanceScore = 0.9;
        critical.estimatedTokens = 100;
        critical.included = false;
        candidates.append(critical);

        AIAPI::BudgetedChunk medium;
        medium.chunkId = "c2";
        medium.content = "Device summary: 5 resistors";
        medium.contentType = "Summary";
        medium.priority = AIAPI::ChunkPriority::MEDIUM;
        medium.relevanceScore = 0.7;
        medium.estimatedTokens = 50;
        medium.included = false;
        candidates.append(medium);

        AIAPI::BudgetedChunk optional;
        optional.chunkId = "c3";
        optional.content = "Similar design from history";
        optional.contentType = "ReferenceDesign";
        optional.priority = AIAPI::ChunkPriority::EXTRA;
        optional.relevanceScore = 0.3;
        optional.estimatedTokens = 200;
        optional.included = false;
        candidates.append(optional);

        auto allocation = AIAPI::ContextBudgetManager::allocate(candidates, 160);

        QCOMPARE(allocation.includedChunks.size(), 2);
        QVERIFY(allocation.totalTokensUsed <= 160);
        QCOMPARE(allocation.chunksDropped, 1);
        QCOMPARE(allocation.includedChunks[0].chunkId, QString("c1"));
    }

    void testBudgetAllocationBalancedMode() {
        QVector<AIAPI::BudgetedChunk> candidates;

        for (int i = 0; i < 5; ++i) {
            AIAPI::BudgetedChunk bc;
            bc.chunkId = QString("chunk_%1").arg(i);
            bc.content = QString("Content for chunk %1").arg(i);
            bc.contentType = "Summary";
            bc.priority = static_cast<AIAPI::ChunkPriority>(i);
            bc.relevanceScore = 0.5;
            bc.estimatedTokens = 50;
            bc.included = false;
            candidates.append(bc);
        }

        auto allocation = AIAPI::ContextBudgetManager::allocate(candidates, 300, AIAPI::PriorityMode::BALANCED);

        QVERIFY(allocation.totalTokensUsed <= 300);
        QVERIFY(allocation.includedChunks.size() >= 1);
    }

    void testBudgetNeverExceedsLimit() {
        QVector<AIAPI::BudgetedChunk> candidates;
        for (int i = 0; i < 20; ++i) {
            AIAPI::BudgetedChunk bc;
            bc.chunkId = QString("chunk_%1").arg(i);
            bc.content = "Some content here that takes space";
            bc.contentType = "Summary";
            bc.priority = AIAPI::ChunkPriority::MEDIUM;
            bc.relevanceScore = 0.5;
            bc.estimatedTokens = 100;
            bc.included = false;
            candidates.append(bc);
        }

        auto allocation = AIAPI::ContextBudgetManager::allocate(candidates, 500);
        QVERIFY(allocation.totalTokensUsed <= 500);
        QCOMPARE(allocation.totalTokensBudget, 500);
    }

    void testAllocationToJson() {
        QVector<AIAPI::BudgetedChunk> candidates;
        AIAPI::BudgetedChunk bc;
        bc.chunkId = "test";
        bc.content = "Test content";
        bc.contentType = "Summary";
        bc.priority = AIAPI::ChunkPriority::MEDIUM;
        bc.relevanceScore = 0.8;
        bc.estimatedTokens = 50;
        bc.included = false;
        candidates.append(bc);

        auto allocation = AIAPI::ContextBudgetManager::allocate(candidates, 1000);
        QJsonObject json = allocation.toJson();

        QVERIFY(json.contains("totalTokensUsed"));
        QVERIFY(json.contains("totalTokensBudget"));
        QVERIFY(json.contains("chunks"));
        QCOMPARE(json["totalTokensBudget"].toInt(), 1000);
    }
};

QTEST_MAIN(TestContextBudget)
#include "test_context_budget.moc"
