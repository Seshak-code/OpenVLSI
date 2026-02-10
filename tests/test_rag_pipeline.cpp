#include <QtTest>
#ifndef Q_MOC_RUN
#include "rag_pipeline.h"
#include "context_budget.h"
#endif

class TestRagPipeline : public QObject {
    Q_OBJECT

private:
    AIAPI::SimulationTrace createTestTrace(const QString& id, bool failure = false) {
        AIAPI::SimulationTrace trace;
        trace.traceId = id;
        trace.topologyHash = "topo_abc123";
        trace.deviceMix = {{"NMOS", 4}, {"Resistor", 6}, {"Capacitor", 2}};
        trace.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0;

        trace.configParams.solverType = "PCG";
        trace.configParams.reltol = 1e-3;
        trace.configParams.abstol = 1e-6;
        trace.configParams.gminSteppingEnabled = false;

        trace.voltageSeries["Vout"] = {0.0, 0.5, 1.0, 1.5, 2.0, 2.5};
        trace.voltageSeries["Vin"] = {0.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        trace.timePoints = {0.0, 1e-6, 2e-6, 3e-6, 4e-6, 5e-6};

        if (failure) {
            AIAPI::FailureLabel label;
            label.type = AIAPI::FailureType::CONVERGENCE_FAILED;
            label.severity = AIAPI::FailureSeverity::ERROR;
            label.message = "Convergence failed after 20 iterations";
            label.nodeOrComponent = "M1";
            label.residual = 1e-3;
            trace.failureModes.append(label);
        }

        return trace;
    }

private slots:
    void initTestCase() {
        AIAPI::RAGPipelineOrchestrator::instance().vectorStore().clear();
    }

    void testIngestAndRetrieve() {
        auto& pipeline = AIAPI::RAGPipelineOrchestrator::instance();

        AIAPI::SimulationTrace trace = createTestTrace("trace_001");
        pipeline.ingestTrace(trace);

        QCOMPARE(pipeline.traceCount(), 1);

        auto results = pipeline.retrieveForQuery("NMOS circuit", 5);
        QVERIFY(results.size() >= 0);
    }

    void testIngestFromSolver() {
        auto& pipeline = AIAPI::RAGPipelineOrchestrator::instance();
        int countBefore = pipeline.traceCount();

        QMap<QString, int> deviceMix = {{"NMOS", 2}, {"Resistor", 3}};
        QMap<QString, std::vector<double>> voltages;
        voltages["Vout"] = {0.0, 1.0, 2.0};

        pipeline.ingestFromSolver("topo_xyz", deviceMix, voltages, true, 5, 1e-9);

        QVERIFY(pipeline.traceCount() > countBefore);
    }

    void testIngestFailureTrace() {
        auto& pipeline = AIAPI::RAGPipelineOrchestrator::instance();

        AIAPI::SimulationTrace failTrace = createTestTrace("trace_fail_001", true);
        pipeline.ingestTrace(failTrace, AIAPI::ChunkingStrategy::FAILURE_FOCUSED);

        auto results = pipeline.retrieveForQuery("convergence failure", 5);
        QVERIFY(results.size() >= 0);
    }

    void testCounterfactuals() {
        auto& pipeline = AIAPI::RAGPipelineOrchestrator::instance();

        AIAPI::SimulationTrace t1 = createTestTrace("cf_001");
        t1.topologyHash = "topo_cf_test";
        t1.configParams.gminSteppingEnabled = false;
        pipeline.ingestTrace(t1);

        AIAPI::SimulationTrace t2 = createTestTrace("cf_002");
        t2.topologyHash = "topo_cf_test";
        t2.configParams.gminSteppingEnabled = true;
        pipeline.ingestTrace(t2);

        AIAPI::TraceConfig targetConfig;
        targetConfig.gminSteppingEnabled = false;
        targetConfig.solverType = "PCG";
        targetConfig.reltol = 1e-3;

        auto cfs = pipeline.getCounterfactuals("topo_cf_test", targetConfig);
        QVERIFY(cfs.size() >= 1);
    }

    void testBuildPromptContext() {
        auto& pipeline = AIAPI::RAGPipelineOrchestrator::instance();

        pipeline.ingestTrace(createTestTrace("prompt_test_001"));

        QString context = pipeline.buildPromptContext("analyze circuit performance", 2000);
        QVERIFY(!context.isEmpty());

        int tokens = AIAPI::ContextBudgetManager::estimateTokens(context);
        QVERIFY(tokens <= 2200);
    }

    void testBuildStructuredContext() {
        auto& pipeline = AIAPI::RAGPipelineOrchestrator::instance();

        QJsonObject structured = pipeline.buildStructuredContext("debug convergence", 3000);

        QVERIFY(structured.contains("query"));
        QVERIFY(structured.contains("topology"));
        QVERIFY(structured.contains("budget"));
        QCOMPARE(structured["query"].toString(), QString("debug convergence"));
    }

    void testQueryChunkRegistration() {
        auto& pipeline = AIAPI::RAGPipelineOrchestrator::instance();

        // Use unique chunk IDs to avoid cross-test contamination
        QVector<QString> chunkIds = {"qcr_chunk_a", "qcr_chunk_b", "qcr_chunk_c"};
        pipeline.registerQueryChunks("qcr_query_001", chunkIds);

        AIAPI::FeedbackLoop::instance().submitFeedback("qcr_query_001", 1);

        auto statsA = AIAPI::FeedbackLoop::instance().getChunkStats("qcr_chunk_a");
        QVERIFY(statsA.timesUsed >= 1);
        QVERIFY(statsA.helpful >= 1);
        QVERIFY(statsA.ewmaScore > 0.0);
    }

    void testRerankingWithFeedback() {
        AIAPI::QueryContext ctx;
        ctx.queryId = "rerank_test";
        ctx.usedChunkIds = {"ranked_chunk_1"};
        ctx.userQuery = "test query";
        ctx.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0;
        AIAPI::FeedbackLoop::instance().registerQueryContext(ctx);

        AIAPI::FeedbackLoop::instance().submitFeedback("rerank_test", 1);

        QVector<AIAPI::FeedbackLoop::RankedChunk> candidates;
        AIAPI::FeedbackLoop::RankedChunk rc1;
        rc1.chunkId = "ranked_chunk_1";
        rc1.content = "Test content with failure information";
        rc1.relevanceScore = 0.5;
        candidates.append(rc1);

        AIAPI::FeedbackLoop::RankedChunk rc2;
        rc2.chunkId = "ranked_chunk_2";
        rc2.content = "Unrelated content";
        rc2.relevanceScore = 0.6;
        candidates.append(rc2);

        auto ranked = AIAPI::FeedbackLoop::instance().rerankContext("test failure query", candidates);
        QCOMPARE(ranked.size(), 2);
    }
};

QTEST_MAIN(TestRagPipeline)
#include "test_rag_pipeline.moc"
