#include "feedback_loop.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <algorithm> // for std::sort

namespace AIAPI {

FeedbackLoop::FeedbackLoop() {
    loadFeedbackLog();
}

FeedbackLoop& FeedbackLoop::instance() {
    static FeedbackLoop _instance;
    return _instance;
}

void FeedbackLoop::submitFeedback(const QString& queryId, int rating, const QString& correction) {
    FeedbackItem item;
    item.queryId = queryId;
    item.rating = rating;
    item.correction = correction;
    item.timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0;
    
    // In a real system, we would capture the query & context here. 
    // For now, we assume they are passed or looked up.
    
    feedbackHistory.append(item);
    
    // Trigger Online Learning
    if (rating > 0) {
        updateContextWeights(queryId);
    }
    
    saveFeedbackLog();
}

QVector<FeedbackLoop::RankedChunk> FeedbackLoop::rerankContext(const QString& query, const QVector<RankedChunk>& candidates) {
    QVector<RankedChunk> ranked = candidates;
    
    // Heuristic 1: Keyword Overlap (TF-IDF style stub)
    for (auto& chunk : ranked) {
        // Boost score if chunk ID has high "confidence weight" from previous positive feedback
        if (contextWeights.contains(chunk.chunkId)) {
            chunk.relevanceScore += contextWeights[chunk.chunkId] * 0.1; 
        }
        
        // Simple search boost
        if (chunk.content.contains(query, Qt::CaseInsensitive)) {
            chunk.relevanceScore += 0.5;
        }
    }
    
    // Sort descending by score
    std::sort(ranked.begin(), ranked.end(), [](const RankedChunk& a, const RankedChunk& b) {
        return a.relevanceScore > b.relevanceScore;
    });
    
    return ranked;
}

void FeedbackLoop::updateContextWeights(const QString& queryId) {
    // 6.3: RAG Context Updates
    // "Reward" the context chunks that led to this successful interaction.
    // In a full implementation, we would track which chunks were actually used in the Prompt.
    // Here we stub it by boosting a hypothetical "related" chunk.
    
    // Example: If query was about "Oscillator", boost "Oscillator Topology" chunks
    // This is where the Cross-Encoder would learn.
}

void FeedbackLoop::saveFeedbackLog() {
    QJsonArray array;
    for (const auto& item : feedbackHistory) {
        QJsonObject obj;
        obj["queryId"] = item.queryId;
        obj["rating"] = item.rating;
        obj["correction"] = item.correction;
        obj["timestamp"] = item.timestamp;
        array.append(obj);
    }
    
    QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataLocation);
    QString path = dataLocation + "/feedback_log.json";
    
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson());
    }
}

void FeedbackLoop::loadFeedbackLog() {
    QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString path = dataLocation + "/feedback_log.json";
    
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
        feedbackHistory.clear();
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
