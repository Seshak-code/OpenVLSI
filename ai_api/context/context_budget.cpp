#include "context_budget.h"
#include <QJsonDocument>
#include <algorithm>

namespace AIAPI {

// ============================================================================
// Token Estimation
// ============================================================================

int ContextBudgetManager::estimateTokens(const QString& text) {
    if (text.isEmpty()) return 0;
    // Heuristic: split on whitespace, multiply by 1.3 for sub-word tokenization
    int wordCount = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
    int estimate = static_cast<int>(wordCount * 1.3);
    // Add 5% headroom (minimum 1 token)
    estimate = static_cast<int>(estimate * 1.05);
    return std::max(estimate, 1);
}

// ============================================================================
// Priority Assignment
// ============================================================================

ChunkPriority ContextBudgetManager::assignPriority(const TraceChunk& chunk, const QJsonObject& context) {
    const QString& type = chunk.contentType;

    // Failure-related chunks are always critical
    if (type == "FailureLog" || type == "DebugContext") {
        return ChunkPriority::CRITICAL;
    }

    // Signal path and probe data are high priority
    if (type == "SignalPath" || type == "ProbeData") {
        return ChunkPriority::HIGH;
    }

    // Summary and device mix are medium
    if (type == "Summary" || type == "DeviceMix" || type == "DeviceCharacteristics") {
        return ChunkPriority::MEDIUM;
    }

    // Waveform data is lower priority (verbose)
    if (type == "Waveform" || type == "WaveformSummary") {
        return ChunkPriority::LOW;
    }

    // Reference designs and historical context are optional
    if (type == "ReferenceDesign" || type == "SimilarTrace") {
        return ChunkPriority::EXTRA;
    }

    // Context-aware boost: if context indicates a failure, promote solver chunks
    if (context.contains("hasFailure") && context["hasFailure"].toBool()) {
        if (type == "SolverDecisions") {
            return ChunkPriority::HIGH;
        }
    }

    return ChunkPriority::MEDIUM;
}

// ============================================================================
// Budget Allocation
// ============================================================================

BudgetAllocation ContextBudgetManager::allocate(QVector<BudgetedChunk>& candidates,
                                                 int maxTokens,
                                                 PriorityMode mode) {
    BudgetAllocation result;
    result.totalTokensBudget = maxTokens;
    result.totalTokensUsed = 0;
    result.chunksDropped = 0;

    // Sort by (priority ASC, relevance DESC)
    std::sort(candidates.begin(), candidates.end(),
              [](const BudgetedChunk& a, const BudgetedChunk& b) {
                  if (static_cast<int>(a.priority) != static_cast<int>(b.priority)) {
                      return static_cast<int>(a.priority) < static_cast<int>(b.priority);
                  }
                  return a.relevanceScore > b.relevanceScore;
              });

    if (mode == PriorityMode::BALANCED) {
        // Guarantee minimum allocation per priority level:
        // CRITICAL: 40%, HIGH: 25%, MEDIUM: 20%, LOW: 10%, OPTIONAL: 5%
        const double tiers[] = {0.40, 0.25, 0.20, 0.10, 0.05};
        int tierBudgets[5];
        for (int i = 0; i < 5; ++i) {
            tierBudgets[i] = static_cast<int>(maxTokens * tiers[i]);
        }

        // First pass: fill each tier up to its budget
        for (auto& chunk : candidates) {
            int tier = static_cast<int>(chunk.priority);
            if (tier >= 0 && tier < 5 && chunk.estimatedTokens <= tierBudgets[tier]) {
                chunk.included = true;
                tierBudgets[tier] -= chunk.estimatedTokens;
                result.totalTokensUsed += chunk.estimatedTokens;
                result.includedChunks.append(chunk);
            } else {
                chunk.included = false;
            }
        }

        // Second pass: use remaining budget for any unfilled chunks
        int remaining = maxTokens - result.totalTokensUsed;
        for (auto& chunk : candidates) {
            if (!chunk.included && chunk.estimatedTokens <= remaining) {
                chunk.included = true;
                remaining -= chunk.estimatedTokens;
                result.totalTokensUsed += chunk.estimatedTokens;
                result.includedChunks.append(chunk);
            }
        }
    } else {
        // STRICT and DEBUG modes: greedy fill by sorted order
        for (auto& chunk : candidates) {
            if (result.totalTokensUsed + chunk.estimatedTokens <= maxTokens) {
                chunk.included = true;
                result.totalTokensUsed += chunk.estimatedTokens;
                result.includedChunks.append(chunk);
            } else {
                chunk.included = false;
                result.chunksDropped++;
            }
        }
    }

    return result;
}

// ============================================================================
// Candidate List Builder
// ============================================================================

QVector<BudgetedChunk> ContextBudgetManager::buildCandidateList(
    const QJsonObject& topology,
    const QJsonObject& probes,
    const QJsonObject& solver,
    const QVector<TraceChunk>& ragChunks,
    const QVector<FeedbackLoop::RankedChunk>& rankedChunks) {

    QVector<BudgetedChunk> candidates;

    // 1. Topology context
    if (!topology.isEmpty()) {
        QString topologyText = QJsonDocument(topology).toJson(QJsonDocument::Compact);
        BudgetedChunk bc;
        bc.chunkId = "topology_context";
        bc.content = topologyText;
        bc.contentType = "Summary";
        bc.priority = ChunkPriority::MEDIUM;
        bc.relevanceScore = 0.8;
        bc.estimatedTokens = estimateTokens(topologyText);
        bc.included = false;
        candidates.append(bc);
    }

    // 2. Probe context
    if (!probes.isEmpty()) {
        QString probeText = QJsonDocument(probes).toJson(QJsonDocument::Compact);
        BudgetedChunk bc;
        bc.chunkId = "probe_context";
        bc.content = probeText;
        bc.contentType = "ProbeData";
        bc.priority = ChunkPriority::HIGH;
        bc.relevanceScore = 0.9;
        bc.estimatedTokens = estimateTokens(probeText);
        bc.included = false;
        candidates.append(bc);
    }

    // 3. Solver context
    if (!solver.isEmpty()) {
        QString solverText = QJsonDocument(solver).toJson(QJsonDocument::Compact);
        BudgetedChunk bc;
        bc.chunkId = "solver_context";
        bc.content = solverText;
        bc.contentType = "SolverDecisions";
        bc.priority = ChunkPriority::MEDIUM;
        bc.relevanceScore = 0.7;
        bc.estimatedTokens = estimateTokens(solverText);
        bc.included = false;
        candidates.append(bc);
    }

    // 4. RAG chunks from trace
    for (const auto& chunk : ragChunks) {
        BudgetedChunk bc;
        bc.chunkId = chunk.chunkId;
        bc.content = chunk.textContent;
        bc.contentType = chunk.contentType;
        bc.priority = assignPriority(chunk);
        bc.relevanceScore = 0.5; // Default; overridden if reranked
        bc.estimatedTokens = estimateTokens(chunk.textContent);
        bc.included = false;
        candidates.append(bc);
    }

    // 5. Reranked chunks from FeedbackLoop
    for (const auto& ranked : rankedChunks) {
        BudgetedChunk bc;
        bc.chunkId = ranked.chunkId;
        bc.content = ranked.content;
        bc.contentType = "RankedResult";
        bc.priority = ChunkPriority::HIGH;
        bc.relevanceScore = ranked.relevanceScore;
        bc.estimatedTokens = estimateTokens(ranked.content);
        bc.included = false;
        candidates.append(bc);
    }

    return candidates;
}

// ============================================================================
// Main Entry Point
// ============================================================================

BudgetAllocation ContextBudgetManager::compressForBudget(
    const QJsonObject& fullContext,
    int maxTokens,
    PriorityMode mode) {

    // Extract sub-contexts
    QJsonObject topology = fullContext["topology"].toObject();
    QJsonObject probes = fullContext["probes"].toObject();
    QJsonObject solver = fullContext["solver"].toObject();

    // No RAG chunks or ranked chunks in this path - those come from pipeline
    QVector<TraceChunk> ragChunks;
    QVector<FeedbackLoop::RankedChunk> rankedChunks;

    auto candidates = buildCandidateList(topology, probes, solver, ragChunks, rankedChunks);
    return allocate(candidates, maxTokens, mode);
}

// ============================================================================
// BudgetAllocation Serialization
// ============================================================================

QJsonObject BudgetAllocation::toJson() const {
    QJsonObject obj;
    obj["totalTokensUsed"] = totalTokensUsed;
    obj["totalTokensBudget"] = totalTokensBudget;
    obj["chunksIncluded"] = includedChunks.size();
    obj["chunksDropped"] = chunksDropped;

    QJsonArray chunks;
    for (const auto& c : includedChunks) {
        QJsonObject co;
        co["chunkId"] = c.chunkId;
        co["contentType"] = c.contentType;
        co["priority"] = static_cast<int>(c.priority);
        co["tokens"] = c.estimatedTokens;
        co["relevance"] = c.relevanceScore;
        chunks.append(co);
    }
    obj["chunks"] = chunks;

    return obj;
}

} // namespace AIAPI
