#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QMap>
#include <QDateTime>
#include "../context/context_engine.h"

namespace AIAPI {

struct FeedbackItem {
    QString queryId;
    QString userQuery;
    QString aiResponse;
    int rating; // 1 (Helpful), -1 (Unhelpful), 0 (Neutral)
    QString correction; // Optional user textual correction
    double timestamp;
    QJsonObject contextSnapshot; // The topology/solver state at time of query
};

/**
 * QueryContext - Tracks which chunks were used to answer a query.
 * Enables attribution-based feedback (reward chunks that led to good answers).
 */
struct QueryContext {
    QString queryId;
    QVector<QString> usedChunkIds;
    QString userQuery;
    double timestamp;
};

/**
 * ChunkStats - Performance statistics for a RAG chunk.
 * Tracks how often a chunk was used and its helpfulness via EWMA.
 */
struct ChunkStats {
    int timesUsed = 0;
    int helpful = 0;
    int unhelpful = 0;
    double ewmaScore = 0.5;  // Exponentially weighted moving average (0-1)
};

class FeedbackLoop {
public:
    static FeedbackLoop& instance();

    // 6.2: Reinforcement Learning Signal
    void submitFeedback(const QString& queryId, int rating, const QString& correction = "");

    // 6.1: Cross-Encoder Reranking
    // Takes a list of candidate RAG chunks and re-orders them based on relevance to the query
    // and historical feedback success.
    struct RankedChunk {
        QString chunkId;
        double relevanceScore;
        QString content;
    };
    QVector<RankedChunk> rerankContext(const QString& query, const QVector<RankedChunk>& candidates);

    // 6.3: RAG Context Updates
    // If feedback is positive, promote the associated context chunks in the vector store.
    void updateContextWeights(const QString& queryId);

    // Query Context Registration
    void registerQueryContext(const QueryContext& ctx);

    // Export training data for fine-tuning
    QJsonArray exportTrainingData() const;

    // Get chunk performance statistics
    ChunkStats getChunkStats(const QString& chunkId) const;

    // Storage Management (Persist feedback to disk for fine-tuning)
    void saveFeedbackLog();
    void loadFeedbackLog();

private:
    FeedbackLoop();
    QVector<FeedbackItem> feedbackHistory;
    QMap<QString, double> contextWeights; // ChunkID -> Learnable Weight
    QMap<QString, QueryContext> queryContexts_; // queryId -> context
    QMap<QString, ChunkStats> chunkPerformance_; // chunkId -> stats

    static constexpr double EWMA_ALPHA = 0.1;
};

} // namespace AIAPI
