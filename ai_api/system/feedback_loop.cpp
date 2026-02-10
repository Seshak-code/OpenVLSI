#include "feedback_loop.h"
#include "../context/safe_io.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace AIAPI {

FeedbackLoop::FeedbackLoop() {
    loadFeedbackLog();
}

FeedbackLoop& FeedbackLoop::instance() {
    static FeedbackLoop _instance;
    return _instance;
}

// ============================================================================
// 6.2: Feedback Submission with EWMA Scoring
// ============================================================================

void FeedbackLoop::submitFeedback(const QString& queryId, int rating, const QString& correction) {
    FeedbackItem item;
    item.queryId = queryId;
    item.rating = rating;
    item.correction = correction;
    item.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0;

    feedbackHistory.append(item);

    // Update chunk stats if we have a query context
    if (queryContexts_.contains(queryId)) {
        const auto& ctx = queryContexts_[queryId];
        for (const auto& chunkId : ctx.usedChunkIds) {
            auto& stats = chunkPerformance_[chunkId];
            stats.timesUsed++;

            if (rating > 0) {
                stats.helpful++;
                // EWMA update: score moves toward 1.0
                stats.ewmaScore = (1.0 - EWMA_ALPHA) * stats.ewmaScore + EWMA_ALPHA * 1.0;
            } else if (rating < 0) {
                stats.unhelpful++;
                // EWMA update: score moves toward 0.0
                stats.ewmaScore = (1.0 - EWMA_ALPHA) * stats.ewmaScore + EWMA_ALPHA * 0.0;
            }
            // Neutral (0) doesn't change EWMA
        }
    }

    // Trigger context weight updates for positive feedback
    if (rating > 0) {
        updateContextWeights(queryId);
    }

    saveFeedbackLog();
}

// ============================================================================
// 6.1: Composite Reranking
// ============================================================================

QVector<FeedbackLoop::RankedChunk> FeedbackLoop::rerankContext(
    const QString& query, const QVector<RankedChunk>& candidates) {

    QVector<RankedChunk> ranked = candidates;
    QStringList queryWords = query.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    for (auto& chunk : ranked) {
        double baseScore = chunk.relevanceScore;

        // 1. Context weight boost (learned from feedback)
        double contextBoost = 0.0;
        if (contextWeights.contains(chunk.chunkId)) {
            contextBoost = contextWeights[chunk.chunkId] * 0.2;
        }

        // 2. Keyword overlap (simple TF-IDF proxy)
        double keywordScore = 0.0;
        QString contentLower = chunk.content.toLower();
        int matchCount = 0;
        for (const auto& word : queryWords) {
            if (word.size() > 2 && contentLower.contains(word)) {
                matchCount++;
            }
        }
        if (!queryWords.isEmpty()) {
            keywordScore = static_cast<double>(matchCount) / queryWords.size();
        }

        // 3. Recency boost (chunks from recent traces score higher)
        double recencyBoost = 0.0;
        if (chunkPerformance_.contains(chunk.chunkId)) {
            // If chunk has been recently used and rated positively, boost
            const auto& stats = chunkPerformance_[chunk.chunkId];
            if (stats.ewmaScore > 0.6) {
                recencyBoost = 0.1;
            }
        }

        // 4. Failure keyword boost
        double failureBoost = 0.0;
        static const QStringList failureKeywords = {
            "fail", "error", "convergence", "diverge", "singular",
            "overflow", "floating", "unstable"
        };
        bool queryHasFailureKeyword = false;
        for (const auto& kw : failureKeywords) {
            if (query.toLower().contains(kw)) {
                queryHasFailureKeyword = true;
                break;
            }
        }
        if (queryHasFailureKeyword && contentLower.contains("fail")) {
            failureBoost = 0.3;
        }

        // Composite score
        chunk.relevanceScore = baseScore + contextBoost + keywordScore
                               + recencyBoost + failureBoost;
    }

    // Sort descending by composite score
    std::sort(ranked.begin(), ranked.end(), [](const RankedChunk& a, const RankedChunk& b) {
        return a.relevanceScore > b.relevanceScore;
    });

    return ranked;
}

// ============================================================================
// 6.3: Context Weight Updates
// ============================================================================

void FeedbackLoop::updateContextWeights(const QString& queryId) {
    if (!queryContexts_.contains(queryId)) return;

    const auto& ctx = queryContexts_[queryId];

    // Promote chunks used in a positively-rated query
    for (const auto& chunkId : ctx.usedChunkIds) {
        double current = contextWeights.value(chunkId, 0.5);
        current = std::min(current + 0.05, 1.0); // Cap at 1.0
        contextWeights[chunkId] = current;
    }

    // Slow decay for unused chunks (apply globally but gently)
    for (auto it = contextWeights.begin(); it != contextWeights.end(); ++it) {
        if (!ctx.usedChunkIds.contains(it.key())) {
            it.value() *= 0.99; // Slow decay
        }
    }
}

// ============================================================================
// Query Context Registration
// ============================================================================

void FeedbackLoop::registerQueryContext(const QueryContext& ctx) {
    queryContexts_[ctx.queryId] = ctx;
}

// ============================================================================
// Training Data Export
// ============================================================================

QJsonArray FeedbackLoop::exportTrainingData() const {
    QJsonArray data;
    for (const auto& item : feedbackHistory) {
        QJsonObject entry;
        entry["queryId"] = item.queryId;
        entry["rating"] = item.rating;
        entry["correction"] = item.correction;
        entry["timestamp"] = item.timestamp;

        // Include chunk attribution if available
        if (queryContexts_.contains(item.queryId)) {
            const auto& ctx = queryContexts_[item.queryId];
            QJsonArray chunks;
            for (const auto& id : ctx.usedChunkIds) {
                chunks.append(id);
            }
            entry["usedChunks"] = chunks;
            entry["userQuery"] = ctx.userQuery;
        }

        data.append(entry);
    }
    return data;
}

// ============================================================================
// Chunk Stats Query
// ============================================================================

ChunkStats FeedbackLoop::getChunkStats(const QString& chunkId) const {
    return chunkPerformance_.value(chunkId);
}

// ============================================================================
// Persistence
// ============================================================================

void FeedbackLoop::saveFeedbackLog() {
    QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataLocation);
    QString path = dataLocation + "/feedback_log.json";

    QJsonObject bundle;

    // 1. Feedback history
    QJsonArray historyArr;
    for (const auto& item : feedbackHistory) {
        QJsonObject obj;
        obj["queryId"] = item.queryId;
        obj["rating"] = item.rating;
        obj["correction"] = item.correction;
        obj["timestamp"] = item.timestamp;
        historyArr.append(obj);
    }
    bundle["feedback"] = historyArr;

    // 2. Context weights
    QJsonObject weightsObj;
    for (auto it = contextWeights.begin(); it != contextWeights.end(); ++it) {
        weightsObj[it.key()] = it.value();
    }
    bundle["weights"] = weightsObj;

    // 3. Chunk performance stats
    QJsonObject statsObj;
    for (auto it = chunkPerformance_.begin(); it != chunkPerformance_.end(); ++it) {
        QJsonObject s;
        s["timesUsed"] = it.value().timesUsed;
        s["helpful"] = it.value().helpful;
        s["unhelpful"] = it.value().unhelpful;
        s["ewmaScore"] = it.value().ewmaScore;
        statsObj[it.key()] = s;
    }
    bundle["chunkStats"] = statsObj;

    SafeIo::write_json_safe(path, bundle, false);
}

void FeedbackLoop::loadFeedbackLog() {
    QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString path = dataLocation + "/feedback_log.json";

    auto result = SafeIo::read_json_file_bounded(path, SafeIo::MAX_AI_CONTEXT_SIZE);
    if (!result.success) return;

    if (result.isObject()) {
        // New bundle format
        QJsonObject bundle = result.toObject();

        // 1. Feedback history
        feedbackHistory.clear();
        QJsonArray historyArr = bundle["feedback"].toArray();
        for (const auto& val : historyArr) {
            QJsonObject obj = val.toObject();
            FeedbackItem item;
            item.queryId = obj["queryId"].toString();
            item.rating = obj["rating"].toInt();
            item.correction = obj["correction"].toString();
            item.timestamp = obj["timestamp"].toDouble();
            feedbackHistory.append(item);
        }

        // 2. Context weights
        contextWeights.clear();
        QJsonObject weightsObj = bundle["weights"].toObject();
        for (auto it = weightsObj.begin(); it != weightsObj.end(); ++it) {
            contextWeights[it.key()] = it.value().toDouble();
        }

        // 3. Chunk performance stats
        chunkPerformance_.clear();
        QJsonObject statsObj = bundle["chunkStats"].toObject();
        for (auto it = statsObj.begin(); it != statsObj.end(); ++it) {
            QJsonObject s = it.value().toObject();
            ChunkStats stats;
            stats.timesUsed = s["timesUsed"].toInt();
            stats.helpful = s["helpful"].toInt();
            stats.unhelpful = s["unhelpful"].toInt();
            stats.ewmaScore = s["ewmaScore"].toDouble(0.5);
            chunkPerformance_[it.key()] = stats;
        }
    } else if (result.isArray()) {
        // Legacy format: bare array of feedback items
        feedbackHistory.clear();
        QJsonArray array = result.toArray();
        for (const auto& val : array) {
            QJsonObject obj = val.toObject();
            FeedbackItem item;
            item.queryId = obj["queryId"].toString();
            item.rating = obj["rating"].toInt();
            item.correction = obj["correction"].toString();
            item.timestamp = obj["timestamp"].toDouble();
            feedbackHistory.append(item);
        }
    }
}

} // namespace AIAPI
