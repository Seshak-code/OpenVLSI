#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <optional>

#include "../simulation/simulation_trace.h"
#include "../simulation/trace_chunker.h"
#include "../simulation/local_vector_store.h"
#include "../system/feedback_loop.h"
#include "../context/context_budget.h"
#include "../context/context_engine.h"

namespace AIAPI {

/**
 * RAGPipelineOrchestrator - Connects the full RAG pipeline:
 *   ingestion -> chunking -> storage -> retrieval -> reranking -> budget -> prompt
 *
 * Singleton that coordinates all AI subsystems to produce
 * token-budgeted, feedback-optimized context for LLM prompts.
 */
class RAGPipelineOrchestrator {
public:
    static RAGPipelineOrchestrator& instance();

    // =========================================================================
    // Ingestion
    // =========================================================================

    /**
     * Ingest a simulation trace: chunk it, store in vector DB, update caches.
     */
    void ingestTrace(const SimulationTrace& trace,
                     ChunkingStrategy strategy = ChunkingStrategy::BALANCED);

    /**
     * Convenience: build and ingest a trace from raw solver outputs.
     */
    void ingestFromSolver(const QString& topologyHash,
                          const QMap<QString, int>& deviceMix,
                          const QMap<QString, std::vector<double>>& nodeVoltages,
                          bool converged, int iterations, double residual);

    // =========================================================================
    // Retrieval
    // =========================================================================

    /**
     * Retrieve and rerank context for a query.
     * Fetches 2x candidates from vector store, reranks via FeedbackLoop,
     * truncates to topK.
     */
    QVector<FeedbackLoop::RankedChunk> retrieveForQuery(const QString& query, int topK = 5);

    /**
     * Get counterfactual traces for What-If analysis.
     */
    QVector<SimulationTrace> getCounterfactuals(const QString& topologyHash,
                                                 const TraceConfig& config);

    // =========================================================================
    // Prompt Building
    // =========================================================================

    /**
     * Build complete prompt context: retrieve + rerank + budget + assemble.
     * Returns a compressed text string suitable for LLM system prompt.
     */
    QString buildPromptContext(const QString& userQuery, int maxTokens = 4000);

    /**
     * Build structured context as JSON (for ADK tool responses).
     */
    QJsonObject buildStructuredContext(const QString& userQuery,
                                       int maxTokens = 4000,
                                       PriorityMode mode = PriorityMode::STRICT_FILL);

    // =========================================================================
    // Attribution
    // =========================================================================

    /**
     * Register which chunks were used for a query (for feedback attribution).
     */
    void registerQueryChunks(const QString& queryId, const QVector<QString>& chunkIds);

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * Get the underlying vector store (for direct queries).
     */
    LocalVectorStore& vectorStore() { return store_; }

    /**
     * Get trace count in vector store.
     */
    int traceCount() const { return store_.count(); }

private:
    RAGPipelineOrchestrator();

    mutable QMutex mutex_;
    LocalVectorStore store_;
};

} // namespace AIAPI
