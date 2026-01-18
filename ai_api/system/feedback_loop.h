#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QMap>
#include "context_engine.h"

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

class FeedbackLoop {
public:
    static FeedbackLoop& instance();

    // 6.2: Reinforcement Learning Signal
    void submitFeedback(const QString& queryId, int rating, const QString& correction = "");
    
    // 6.1: Cross-Encoder Reranking Stub
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

    // Storage Management (Persist feedback to disk for fine-tuning)
    void saveFeedbackLog();
    void loadFeedbackLog();

private:
    FeedbackLoop();
    QVector<FeedbackItem> feedbackHistory;
    QMap<QString, double> contextWeights; // ChunkID -> Learnable Weight
};

} // namespace AIAPI
