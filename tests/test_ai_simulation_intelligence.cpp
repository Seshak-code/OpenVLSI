#include <QtTest>
#include "ai_api/simulation/trace_chunker.h"
#include "ai_api/simulation/local_vector_store.h"
#include "ai_api/simulation/speculative_indexing.h"

class TestAISimulationIntelligence : public QObject {
    Q_OBJECT

private slots:
    void testTraceChunkerSummary() {
        AIAPI::SimulationTrace trace;
        trace.traceId = "test-trace-001";
        trace.topologyHash = "0xABCD1234";
        trace.deviceMix["Resistor"] = 5;
        trace.deviceMix["MOSFET"] = 2;
        trace.configParams.solverType = "LU";
        trace.configParams.reltol = 1e-3;
        trace.configParams.abstol = 1e-12;
        
        auto chunks = AIAPI::TraceChunker::chunk(trace, AIAPI::ChunkingStrategy::SUMMARY_ONLY);
        
        QVERIFY(chunks.size() == 1);
        QCOMPARE(chunks[0].contentType, QString("Summary"));
        QVERIFY(chunks[0].textContent.contains("SUCCEEDED"));
        QVERIFY(chunks[0].textContent.contains("7 devices")); // 5+2
    }
    
    void testTraceChunkerBalanced() {
        AIAPI::SimulationTrace trace;
        trace.traceId = "test-trace-002";
        trace.topologyHash = "0x12345678";
        trace.deviceMix["BJT"] = 3;
        trace.configParams.solverType = "PCG";
        
        // Add waveform data
        trace.voltageSeries["Node1"] = {0.0, 1.0, 2.0, 3.3};
        
        auto chunks = AIAPI::TraceChunker::chunk(trace, AIAPI::ChunkingStrategy::BALANCED);
        
        QVERIFY(chunks.size() >= 3); // Summary + DeviceMix + WaveformSummary
        
        // Check for expected content types
        bool hasSummary = false, hasDeviceMix = false, hasWaveformSummary = false;
        for (const auto& chunk : chunks) {
            if (chunk.contentType == "Summary") hasSummary = true;
            if (chunk.contentType == "DeviceMix") hasDeviceMix = true;
            if (chunk.contentType == "WaveformSummary") hasWaveformSummary = true;
        }
        QVERIFY(hasSummary);
        QVERIFY(hasDeviceMix);
        QVERIFY(hasWaveformSummary);
    }
    
    void testTraceChunkerFailureFocused() {
        AIAPI::SimulationTrace trace;
        trace.traceId = "test-trace-003";
        trace.topologyHash = "0xFAILED00";
        trace.deviceMix["Resistor"] = 1;
        
        // Add a failure
        AIAPI::TraceFailure failure;
        failure.failureType = "Singular Jacobian";
        failure.description = "Matrix is singular at node 5";
        failure.timestamp = 0.0;
        failure.involvedNodes.append("Node5");
        trace.failureModes.append(failure);
        
        auto chunks = AIAPI::TraceChunker::chunk(trace, AIAPI::ChunkingStrategy::FAILURE_FOCUSED);
        
        QVERIFY(trace.isFailure());
        
        // Should have failure chunk
        bool hasFailureLog = false;
        for (const auto& chunk : chunks) {
            if (chunk.contentType == "FailureLog") {
                hasFailureLog = true;
                QVERIFY(chunk.textContent.contains("Singular Jacobian"));
            }
        }
        QVERIFY(hasFailureLog);
    }
    
    void testLocalVectorStoreUpsert() {
        AIAPI::LocalVectorStore store;
        
        AIAPI::SimulationTrace trace;
        trace.traceId = "store-test-001";
        trace.topologyHash = "0xTOPO0001";
        
        QVERIFY(store.upsertTrace(trace));
        QCOMPARE(store.count(), 1);
        QVERIFY(store.hasTrace("store-test-001"));
    }
    
    void testLocalVectorStoreQuery() {
        AIAPI::LocalVectorStore store;
        
        // Insert a failed trace
        AIAPI::SimulationTrace failedTrace;
        failedTrace.traceId = "query-test-001";
        failedTrace.topologyHash = "0xFAIL";
        AIAPI::TraceFailure f; f.failureType = "Convergence"; f.description = "Failed";
        failedTrace.failureModes.append(f);
        store.upsertTrace(failedTrace);
        
        // Insert a successful trace
        AIAPI::SimulationTrace successTrace;
        successTrace.traceId = "query-test-002";
        successTrace.topologyHash = "0xOK";
        store.upsertTrace(successTrace);
        
        // Query for failures
        auto results = store.querySimilar("failed convergence", 5);
        QVERIFY(results.size() >= 1);
        
        // At least one should be the failed trace
        bool foundFailed = false;
        for (const auto& r : results) {
            if (r.isFailure()) foundFailed = true;
        }
        QVERIFY(foundFailed);
    }
    
    void testLocalVectorStoreCounterfactuals() {
        AIAPI::LocalVectorStore store;
        
        // Insert two traces with same topology but different configs
        AIAPI::SimulationTrace trace1;
        trace1.traceId = "cf-test-001";
        trace1.topologyHash = "0xSAME";
        trace1.configParams.gminSteppingEnabled = false;
        store.upsertTrace(trace1);
        
        AIAPI::SimulationTrace trace2;
        trace2.traceId = "cf-test-002";
        trace2.topologyHash = "0xSAME";
        trace2.configParams.gminSteppingEnabled = true;
        store.upsertTrace(trace2);
        
        // Find counterfactuals (same topology, different config)
        AIAPI::TraceConfig targetConfig;
        targetConfig.gminSteppingEnabled = false;
        
        auto counterfactuals = store.findCounterfactuals("0xSAME", targetConfig);
        
        // Should find at least one with different config
        QVERIFY(counterfactuals.size() >= 1);
        bool foundDifferent = false;
        for (const auto& cf : counterfactuals) {
            if (cf.configParams.gminSteppingEnabled != targetConfig.gminSteppingEnabled) {
                foundDifferent = true;
            }
        }
        QVERIFY(foundDifferent);
    }

    void testSpeculativeManager() {
        auto store = std::make_shared<AIAPI::LocalVectorStore>();
        AIAPI::SpeculativeManager manager(store);
        
        // Setup minimal netlist
        TensorNetlist netlist;
        netlist.numGlobalNodes = 2; // Node 1 and 2
        // Add a resistor to ensure physics runs
        Resistor r;
        r.nodeTerminal1 = 1;
        r.nodeTerminal2 = 2;
        r.resistance_ohms = 1000.0;
        netlist.globalBlock.resistors.push_back(r);
        
        AIAPI::SpeculativeOverrides overrides;
        overrides.timeStepOverride = 1e-9;
        
        // Run Async
        QFuture<QString> future = manager.runAndIndexAsync(netlist, overrides);
        future.waitForFinished();
        QString traceId = future.result();
        
        QVERIFY(!traceId.isEmpty());
        
        // Verify it's in the store
        QVERIFY(store->hasTrace(traceId));
        
        // Verify we can find it as counterfactual
        AIAPI::TraceConfig config;
        // The default config in SpeculativeRunner uses predictionEnabled=true
        // Let's query for something with that config logic from LocalVectorStore
        // Actually, store->findCounterfactuals matches by topologyHash.
        // SpeculativeRunner uses dummy hash "12345".
        
        AIAPI::SimulationTrace stored = store->getTrace(traceId);
        QCOMPARE(stored.topologyHash, QString("12345"));
        QCOMPARE(stored.configParams.predictionEnabled, true);
        
        // Check chunks were generated
        QVERIFY(!stored.ragChunks.isEmpty());
    }
};


QTEST_MAIN(TestAISimulationIntelligence)
#include "test_ai_simulation_intelligence.moc"
