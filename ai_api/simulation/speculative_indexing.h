#pragma once

#include "speculative_runner.h"
#include "local_vector_store.h"
#include "trace_chunker.h"
#include <QFuture>
#include <QtConcurrent>

namespace AIAPI {

/**
 * SpeculativeManager - Orchestrates "What-If" simulations and knowledge indexing.
 * 
 * Responsibilities:
 * 1. Execute speculative runs (async)
 * 2. Auto-chunk and index results into VectorStore
 * 3. Provide retrieval for similar counterfactuals
 */
class SpeculativeManager {
public:
    explicit SpeculativeManager(std::shared_ptr<VectorStoreInterface> vectorStore)
        : vectorStore_(vectorStore) {}

    /**
     * Run a speculative simulation and automatically index the result.
     * Returns a Future yielding the Trace ID.
     */
    QFuture<QString> runAndIndexAsync(
        const TensorNetlist& netlist,
        const SpeculativeOverrides& overrides
    ) {
        return QtConcurrent::run([this, netlist, overrides]() -> QString {
            // 1. Run Simulation
            SimulationTrace trace = SpeculativeRunner::runSpeculative(netlist, overrides);
            
            // 2. Generate chunks (if not already done by runner, but runner should be pure)
            // Note: TraceBuilder logic might be replicated here or TraceRunner should use Builder
            if (trace.ragChunks.isEmpty()) {
                trace.ragChunks = TraceChunker::chunk(trace, ChunkingStrategy::BALANCED);
            }
            
            // 3. Index into Vector Store
            vectorStore_->upsertTrace(trace);
            
            return trace.traceId;
        });
    }

    /**
     * Retrieve past speculative results for a specific topology.
     */
    QVector<SimulationTrace> getCounterfactuals(
        const QString& topologyHash,
        const TraceConfig& targetConfig
    ) {
        return vectorStore_->findCounterfactuals(topologyHash, targetConfig);
    }

private:
    std::shared_ptr<VectorStoreInterface> vectorStore_;
};

} // namespace AIAPI
