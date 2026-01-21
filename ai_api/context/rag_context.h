#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QList>
#include <QUuid>
#include "../analysis/failure_taxonomy.h"

namespace AIAPI {

// ============================================================================
// RAG CONTEXT - Structured knowledge objects for retrieval-augmented generation
// ============================================================================

/**
 * SimulationKnowledgeObject - A self-contained snapshot of simulation state
 * suitable for embedding in a vector store or injecting into LLM prompts.
 */
struct SimulationKnowledgeObject {
    QString traceId;              // Unique ID for this simulation trace
    QString topologyHash;         // Hash of the circuit topology
    QJsonArray deviceMix;         // [{type: "NMOS", count: 4}, ...]
    QList<FailureLabel> failures; // Labeled failure modes
    QJsonObject waveformSummary;  // {vmax, vmin, settling_time, overshoot}
    QJsonObject configSnapshot;   // Simulation parameters (temp, corners, etc.)
    QDateTime timestamp;          // When this trace was captured
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["traceId"] = traceId;
        obj["topologyHash"] = topologyHash;
        obj["deviceMix"] = deviceMix;
        obj["failures"] = FailureLabeler::toJsonArray(failures);
        obj["waveformSummary"] = waveformSummary;
        obj["config"] = configSnapshot;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        return obj;
    }
};

// ============================================================================
// USER FEEDBACK LEARNING - Capture approval/rejection signals for model tuning
// ============================================================================

/**
 * UserFeedback - Records user approval/rejection for a parsed intent.
 * Used to learn user preferences and improve NL parsing accuracy.
 */
struct UserFeedback {
    QString originalIntent;    // Raw user input: "add a 10k resistor"
    QString parsedAction;      // Mapped action: "placeComponent:Resistor:10000"
    double confidence;         // Parser confidence (0-1)
    bool wasApproved;          // Did user click Approve?
    QString rejectionReason;   // If rejected, why? (optional user input)
    QDateTime timestamp;
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["intent"] = originalIntent;
        obj["action"] = parsedAction;
        obj["confidence"] = confidence;
        obj["approved"] = wasApproved;
        obj["rejectionReason"] = rejectionReason;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        return obj;
    }
    
    static UserFeedback fromJson(const QJsonObject& obj) {
        UserFeedback fb;
        fb.originalIntent = obj["intent"].toString();
        fb.parsedAction = obj["action"].toString();
        fb.confidence = obj["confidence"].toDouble(0.5);
        fb.wasApproved = obj["approved"].toBool();
        fb.rejectionReason = obj["rejectionReason"].toString();
        fb.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        return fb;
    }
};

/**
 * ConfidenceExplanation - Explains why AI confidence is < 100%.
 */
struct ConfidenceExplanation {
    double score;              // 0-100%
    QStringList reasons;       // Why confidence is reduced
    QJsonArray alternatives;   // Alternative interpretations if score < 80%
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["score"] = score;
        QJsonArray reasonsArr;
        for (const auto& r : reasons) reasonsArr.append(r);
        obj["reasons"] = reasonsArr;
        obj["alternatives"] = alternatives;
        return obj;
    }
};

/**
 * FeedbackStore - Persists and retrieves user feedback for learning.
 * Storage location: ~/.acutesim/feedback.json
 */
class FeedbackStore {
public:
    /**
     * Record a new feedback entry.
     */
    static void recordFeedback(const UserFeedback& feedback);
    
    /**
     * Load all historical feedback entries.
     */
    static QList<UserFeedback> loadHistory();
    
    /**
     * Analyze feedback to detect patterns.
     * Returns common corrections (e.g., "user prefers kohm over ohm").
     */
    static QJsonObject analyzePatterns();
    
    /**
     * Get approval rate for a specific action type.
     */
    static double getApprovalRate(const QString& actionType);
    
private:
    static QString getFeedbackFilePath();
};

/**
 * RAGContextBuilder - Constructs rich context for LLM prompts.
 */
class RAGContextBuilder {
public:
    /**
     * Build a complete RAG context object from current simulation state.
     */
    static SimulationKnowledgeObject buildFromCurrentState(
        const QJsonObject& circuitState,
        const QJsonObject& solverStats,
        const QJsonObject& probeValues
    );
    
    /**
     * Compress context to fit within token limits.
     * Uses topology-aware summarization.
     */
    static QJsonObject compressContext(const SimulationKnowledgeObject& fullContext,
                                        int maxTokens = 4000);
    
    /**
     * Generate confidence explanation for a parsed intent.
     */
    static ConfidenceExplanation explainConfidence(
        const QString& intent,
        const QString& action,
        double rawConfidence
    );
};

} // namespace AIAPI
