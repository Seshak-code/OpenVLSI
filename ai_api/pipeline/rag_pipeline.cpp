#include "rag_pipeline.h"
#include "../analysis/topology_analyzer.h"
#include <QJsonDocument>
#include <QUuid>
#include <QDateTime>

namespace AIAPI {

RAGPipelineOrchestrator::RAGPipelineOrchestrator() = default;

RAGPipelineOrchestrator& RAGPipelineOrchestrator::instance() {
    static RAGPipelineOrchestrator _instance;
    return _instance;
}

// ============================================================================
// Ingestion
// ============================================================================

void RAGPipelineOrchestrator::ingestTrace(const SimulationTrace& trace,
                                           ChunkingStrategy strategy) {
    QMutexLocker locker(&mutex_);

    // 1. Chunk the trace
    auto chunks = TraceChunker::chunk(trace, strategy);

    // 2. Create a mutable copy with chunks attached
    SimulationTrace enriched = trace;
    enriched.ragChunks = chunks;

    // 3. Upsert to vector store
    store_.upsertTrace(enriched);

    // 4. Update the ContextEngine's last trace cache
    ContextEngine::setLastTrace(trace);
}

void RAGPipelineOrchestrator::ingestFromSolver(
    const QString& topologyHash,
    const QMap<QString, int>& deviceMix,
    const QMap<QString, std::vector<double>>& nodeVoltages,
    bool converged, int iterations, double residual) {

    SimulationTrace trace;
    trace.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    trace.topologyHash = topologyHash;
    trace.deviceMix = deviceMix;
    trace.voltageSeries = nodeVoltages;
    trace.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0;

    // Build config from solver stats
    trace.configParams.solverType = "PCG";
    trace.configParams.reltol = 1e-3;
    trace.configParams.abstol = 1e-6;

    // Label failures if not converged
    if (!converged) {
        FailureLabel label;
        label.type = FailureType::CONVERGENCE_FAILED;
        label.severity = FailureSeverity::ERROR;
        label.message = QString("Convergence failed after %1 iterations. Residual: %2")
                            .arg(iterations).arg(residual, 0, 'e', 3);
        label.timestamp = trace.timestamp;
        trace.failureModes.append(label);
    }

    ingestTrace(trace);
}

// ============================================================================
// Retrieval
// ============================================================================

QVector<FeedbackLoop::RankedChunk> RAGPipelineOrchestrator::retrieveForQuery(
    const QString& query, int topK) {
    QMutexLocker locker(&mutex_);

    // Fetch 2x candidates from vector store
    auto traces = store_.querySimilar(query, topK * 2);

    // Convert trace chunks to RankedChunks for the feedback loop
    QVector<FeedbackLoop::RankedChunk> candidates;
    for (const auto& trace : traces) {
        for (const auto& chunk : trace.ragChunks) {
            FeedbackLoop::RankedChunk rc;
            rc.chunkId = chunk.chunkId;
            rc.content = chunk.textContent;
            rc.relevanceScore = 0.5; // Base score
            candidates.append(rc);
        }
    }

    locker.unlock();

    // Rerank via FeedbackLoop
    auto ranked = FeedbackLoop::instance().rerankContext(query, candidates);

    // Truncate to topK
    if (ranked.size() > topK) {
        ranked.resize(topK);
    }

    return ranked;
}

QVector<SimulationTrace> RAGPipelineOrchestrator::getCounterfactuals(
    const QString& topologyHash, const TraceConfig& config) {
    QMutexLocker locker(&mutex_);
    return store_.findCounterfactuals(topologyHash, config);
}

// ============================================================================
// Prompt Building
// ============================================================================

QString RAGPipelineOrchestrator::buildPromptContext(const QString& userQuery, int maxTokens) {
    // 1. Retrieve and rerank
    auto rankedChunks = retrieveForQuery(userQuery, 10);

    // 2. Get live circuit context
    QJsonObject topology = ContextEngine::serializeTopologyChunks();
    QJsonObject probes = ContextEngine::serializeProbeContext();
    QJsonObject solver = ContextEngine::serializeSolverDecisions();

    // 3. Get topology compression
    QJsonObject topologyAnalysis = TopologyAnalyzer::toJson();
    QString compressedTopology = topologyAnalysis["compressedPrompt"].toString();

    // 4. Build candidate list for budget allocation
    QVector<TraceChunk> emptyRagChunks; // Live chunks come from context, not RAG
    auto candidates = ContextBudgetManager::buildCandidateList(
        topology, probes, solver, emptyRagChunks, rankedChunks);

    // 5. Allocate budget
    auto allocation = ContextBudgetManager::allocate(candidates, maxTokens);

    // 6. Assemble prompt text
    QString prompt;
    prompt += compressedTopology + "\n\n";

    for (const auto& chunk : allocation.includedChunks) {
        prompt += chunk.content + "\n";
    }

    return prompt;
}

QJsonObject RAGPipelineOrchestrator::buildStructuredContext(
    const QString& userQuery, int maxTokens, PriorityMode mode) {

    auto rankedChunks = retrieveForQuery(userQuery, 10);

    QJsonObject topology = ContextEngine::serializeTopologyChunks();
    QJsonObject probes = ContextEngine::serializeProbeContext();
    QJsonObject solver = ContextEngine::serializeSolverDecisions();

    QVector<TraceChunk> emptyRagChunks;
    auto candidates = ContextBudgetManager::buildCandidateList(
        topology, probes, solver, emptyRagChunks, rankedChunks);

    auto allocation = ContextBudgetManager::allocate(candidates, maxTokens, mode);

    QJsonObject result;
    result["query"] = userQuery;
    result["topology"] = topology;
    result["probes"] = probes;
    result["solver"] = solver;
    result["budget"] = allocation.toJson();

    // Include ranked chunks
    QJsonArray rankedArr;
    for (const auto& rc : rankedChunks) {
        QJsonObject rco;
        rco["chunkId"] = rc.chunkId;
        rco["relevance"] = rc.relevanceScore;
        rco["content"] = rc.content;
        rankedArr.append(rco);
    }
    result["rankedChunks"] = rankedArr;

    return result;
}

// ============================================================================
// Attribution
// ============================================================================

void RAGPipelineOrchestrator::registerQueryChunks(const QString& queryId,
                                                   const QVector<QString>& chunkIds) {
    QueryContext ctx;
    ctx.queryId = queryId;
    ctx.usedChunkIds = chunkIds;
    ctx.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0;

    FeedbackLoop::instance().registerQueryContext(ctx);
}

} // namespace AIAPI
