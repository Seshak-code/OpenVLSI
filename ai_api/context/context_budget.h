#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include "../simulation/simulation_trace.h"
#include "../system/feedback_loop.h"

namespace AIAPI {

/**
 * ChunkPriority - Priority levels for context budget allocation.
 * Lower numeric value = higher priority (allocated first).
 */
enum class ChunkPriority {
    CRITICAL = 0,   // Failure diagnostics, convergence errors
    HIGH     = 1,   // Signal path analysis, active probes
    MEDIUM   = 2,   // Passive device tables, topology summary
    LOW      = 3,   // Waveform summaries, reference designs
    OPTIONAL = 4    // Historical context, similar designs
};

/**
 * PriorityMode - Controls allocation strategy.
 */
enum class PriorityMode {
    STRICT,     // Fill highest priority first, then next
    BALANCED,   // Guarantee minimum allocation per priority level
    DEBUG       // Prioritize failure and solver context
};

/**
 * BudgetedChunk - A context chunk annotated with priority and token estimate.
 */
struct BudgetedChunk {
    QString chunkId;
    QString content;
    QString contentType;       // Maps to TraceChunk.contentType
    ChunkPriority priority;
    double relevanceScore;     // 0.0-1.0 from reranking
    int estimatedTokens;
    bool included;             // Set by allocator
};

/**
 * BudgetAllocation - Result of budget allocation.
 */
struct BudgetAllocation {
    QVector<BudgetedChunk> includedChunks;
    int totalTokensUsed;
    int totalTokensBudget;
    int chunksDropped;
    QJsonObject toJson() const;
};

/**
 * ContextBudgetManager - Token-aware context allocation for LLM prompts.
 *
 * Estimates token counts, assigns priorities based on content type,
 * and performs greedy knapsack allocation within a token budget.
 */
class ContextBudgetManager {
public:
    /**
     * Estimate token count for a string.
     * Uses word-based heuristic: tokens ~= words * 1.3 + 5% headroom.
     */
    static int estimateTokens(const QString& text);

    /**
     * Assign priority to a trace chunk based on content type and context.
     */
    static ChunkPriority assignPriority(const TraceChunk& chunk, const QJsonObject& context = {});

    /**
     * Greedy budget allocation sorted by (priority ASC, relevance DESC).
     * Returns allocation result with included/excluded chunks.
     */
    static BudgetAllocation allocate(QVector<BudgetedChunk>& candidates,
                                     int maxTokens,
                                     PriorityMode mode = PriorityMode::STRICT);

    /**
     * Build candidate list from all available context sources.
     */
    static QVector<BudgetedChunk> buildCandidateList(
        const QJsonObject& topology,
        const QJsonObject& probes,
        const QJsonObject& solver,
        const QVector<TraceChunk>& ragChunks,
        const QVector<FeedbackLoop::RankedChunk>& rankedChunks);

    /**
     * Main entry point: compress full context to fit within token budget.
     */
    static BudgetAllocation compressForBudget(
        const QJsonObject& fullContext,
        int maxTokens,
        PriorityMode mode = PriorityMode::STRICT);
};

} // namespace AIAPI
