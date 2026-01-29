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
        obj["failures"] = QJsonArray::fromVariantList(FailureLabeler::toVariantList(failures));
        obj["waveformSummary"] = waveformSummary;
        obj["config"] = configSnapshot;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        return obj;
    }
};

/**
 * ReferenceDesignContext - Few-shot learning context for AI circuit generation.
 * Provides structured examples of validated analog designs.
 */
struct ReferenceDesignContext {
    QString designType;          // "LNA", "OpAmp", "Bandgap"
    QString description;         // Human-readable description
    QJsonObject topology;        // {stages: 2, compensation: "miller", etc.}
    QJsonArray deviceList;       // [{type: "NMOS", role: "diff_pair", W: "10u", L: "500n"}]
    QJsonObject designChoices;   // {why_cascode: "noise", why_miller: "stability"}
    QJsonObject simResults;      // {gain_dB: 45, phase_margin: 60, Vref: 1.25}
    QJsonObject spiceNetlist;    // Exportable SPICE representation
    QString goldenStatus;        // "VERIFIED", "STALE", "PENDING"
    double ngspiceCorrelation;   // Correlation with ngspice (0-1)
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["designType"] = designType;
        obj["description"] = description;
        obj["topology"] = topology;
        obj["deviceList"] = deviceList;
        obj["designChoices"] = designChoices;
        obj["simResults"] = simResults;
        obj["goldenStatus"] = goldenStatus;
        obj["ngspiceCorrelation"] = ngspiceCorrelation;
        return obj;
    }
    
    /**
     * Generate prompt injection text for LLM.
     */
    QString toPromptText() const {
        QString text;
        text += QString("=== Reference Design: %1 ===\n").arg(designType);
        text += QString("Description: %1\n").arg(description);
        text += QString("Golden Status: %1 (ngspice correlation: %2)\n")
                .arg(goldenStatus).arg(ngspiceCorrelation, 0, 'f', 4);
        text += QString("Key Metrics: Gain=%1dB, PM=%2°\n")
                .arg(simResults["gain_dB"].toDouble(), 0, 'f', 1)
                .arg(simResults["phase_margin"].toDouble(), 0, 'f', 1);
        return text;
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

/**
 * ReferenceDesignProvider - Provides reference designs for AI few-shot learning.
 * 
 * Enables "create more like this" by injecting verified analog designs into
 * LLM context as structured examples.
 */
class ReferenceDesignProvider {
public:
    /**
     * Get all available reference design contexts.
     */
    static QList<ReferenceDesignContext> getAll();
    
    /**
     * Get reference designs by type (LNA, OpAmp, Bandgap).
     */
    static QList<ReferenceDesignContext> getByType(const QString& designType);
    
    /**
     * Get only VERIFIED (golden) designs.
     */
    static QList<ReferenceDesignContext> getVerifiedOnly();
    
    /**
     * Build LNA reference context with default parameters.
     */
    static ReferenceDesignContext buildLNAContext();
    
    /**
     * Build OpAmp reference context with default parameters.
     */
    static ReferenceDesignContext buildOpAmpContext();
    
    /**
     * Build Bandgap reference context with default parameters.
     */
    static ReferenceDesignContext buildBandgapContext();
    
    /**
     * Generate combined prompt text for LLM injection.
     * Includes all verified designs as few-shot examples.
     */
    static QString generateFewShotPrompt(int maxTokens = 2000);
    
    /**
     * Check if a design's verification is stale (topology changed since last verify).
     */
    static bool isStale(const QString& designType, const QString& currentTopologyHash);
};

} // namespace AIAPI
