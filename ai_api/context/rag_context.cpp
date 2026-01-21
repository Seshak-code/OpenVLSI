#include "rag_context.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>

namespace AIAPI {

// ============================================================================
// FeedbackStore Implementation
// ============================================================================

QString FeedbackStore::getFeedbackFilePath() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + "/.acutesim";
    }
    QDir().mkpath(configDir);
    return configDir + "/feedback.json";
}

void FeedbackStore::recordFeedback(const UserFeedback& feedback) {
    QList<UserFeedback> history = loadHistory();
    history.append(feedback);
    
    // Limit to last 1000 entries to prevent unbounded growth
    while (history.size() > 1000) {
        history.removeFirst();
    }
    
    QJsonArray arr;
    for (const auto& fb : history) {
        arr.append(fb.toJson());
    }
    
    QFile file(getFeedbackFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        file.close();
    }
}

QList<UserFeedback> FeedbackStore::loadHistory() {
    QList<UserFeedback> result;
    
    QFile file(getFeedbackFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        
        if (doc.isArray()) {
            for (const auto& val : doc.array()) {
                result.append(UserFeedback::fromJson(val.toObject()));
            }
        }
    }
    
    return result;
}

QJsonObject FeedbackStore::analyzePatterns() {
    QJsonObject patterns;
    QList<UserFeedback> history = loadHistory();
    
    // Track common rejections
    QMap<QString, int> rejectionCounts;
    QMap<QString, int> approvalCounts;
    
    for (const auto& fb : history) {
        QString key = fb.parsedAction.split(":").first(); // Action type
        if (fb.wasApproved) {
            approvalCounts[key]++;
        } else {
            rejectionCounts[key]++;
        }
    }
    
    // Find actions with high rejection rates
    QJsonArray problematicActions;
    for (auto it = rejectionCounts.begin(); it != rejectionCounts.end(); ++it) {
        int total = it.value() + approvalCounts.value(it.key(), 0);
        double rejectionRate = static_cast<double>(it.value()) / total;
        
        if (rejectionRate > 0.3 && total >= 5) { // >30% rejection with sufficient samples
            QJsonObject action;
            action["action"] = it.key();
            action["rejectionRate"] = rejectionRate;
            action["samples"] = total;
            problematicActions.append(action);
        }
    }
    
    patterns["problematicActions"] = problematicActions;
    patterns["totalFeedback"] = history.size();
    
    return patterns;
}

double FeedbackStore::getApprovalRate(const QString& actionType) {
    QList<UserFeedback> history = loadHistory();
    
    int approved = 0;
    int total = 0;
    
    for (const auto& fb : history) {
        if (fb.parsedAction.startsWith(actionType)) {
            total++;
            if (fb.wasApproved) approved++;
        }
    }
    
    return (total > 0) ? static_cast<double>(approved) / total : 0.5;
}

// ============================================================================
// RAGContextBuilder Implementation
// ============================================================================

SimulationKnowledgeObject RAGContextBuilder::buildFromCurrentState(
    const QJsonObject& circuitState,
    const QJsonObject& solverStats,
    const QJsonObject& probeValues
) {
    SimulationKnowledgeObject obj;
    
    obj.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    obj.topologyHash = circuitState["topologyHash"].toString();
    obj.timestamp = QDateTime::currentDateTime();
    obj.configSnapshot = solverStats;
    
    // Build device mix
    QMap<QString, int> deviceCounts;
    QJsonArray components = circuitState["components"].toArray();
    for (const auto& comp : components) {
        QString type = comp.toObject()["type"].toString();
        deviceCounts[type]++;
    }
    
    QJsonArray mixArr;
    for (auto it = deviceCounts.begin(); it != deviceCounts.end(); ++it) {
        QJsonObject entry;
        entry["type"] = it.key();
        entry["count"] = it.value();
        mixArr.append(entry);
    }
    obj.deviceMix = mixArr;
    
    // Label failures
    obj.failures = FailureLabeler::labelFailures(solverStats, circuitState);
    
    // Waveform summary (if available)
    if (probeValues.contains("waveforms")) {
        obj.waveformSummary = probeValues["waveforms"].toObject();
    }
    
    return obj;
}

QJsonObject RAGContextBuilder::compressContext(
    const SimulationKnowledgeObject& fullContext,
    int maxTokens
) {
    QJsonObject compressed;
    
    // Always include core identifiers
    compressed["traceId"] = fullContext.traceId;
    compressed["topologyHash"] = fullContext.topologyHash;
    
    // Include device mix (low token cost)
    compressed["deviceMix"] = fullContext.deviceMix;
    
    // Include failures (critical for debugging)
    compressed["failures"] = FailureLabeler::toJsonArray(fullContext.failures);
    
    // Conditionally include waveform summary based on token budget
    QJsonDocument doc(fullContext.waveformSummary);
    int waveformTokens = doc.toJson().size() / 4; // Rough estimate
    
    if (waveformTokens < maxTokens / 4) {
        compressed["waveformSummary"] = fullContext.waveformSummary;
    } else {
        // Summarize key metrics only
        QJsonObject summary;
        if (fullContext.waveformSummary.contains("vmax"))
            summary["vmax"] = fullContext.waveformSummary["vmax"];
        if (fullContext.waveformSummary.contains("settling_time"))
            summary["settling_time"] = fullContext.waveformSummary["settling_time"];
        compressed["waveformSummary"] = summary;
    }
    
    return compressed;
}

ConfidenceExplanation RAGContextBuilder::explainConfidence(
    const QString& intent,
    const QString& action,
    double rawConfidence
) {
    ConfidenceExplanation explanation;
    explanation.score = rawConfidence * 100.0;
    
    // Analyze why confidence might be reduced
    if (rawConfidence < 1.0) {
        // Check for ambiguity indicators
        if (intent.contains(" or ") || intent.contains("?")) {
            explanation.reasons.append("User input contains ambiguous phrasing");
        }
        
        // Check for uncommon patterns
        if (intent.split(" ").size() < 3) {
            explanation.reasons.append("Command is very short, limited context");
        }
        
        // Check if component type is ambiguous
        QStringList ambiguousTerms = {"component", "part", "thing", "element"};
        for (const auto& term : ambiguousTerms) {
            if (intent.toLower().contains(term)) {
                explanation.reasons.append(QString("Generic term '%1' could match multiple component types").arg(term));
                break;
            }
        }
        
        // Check for value ambiguity (e.g., "10k" vs "10K" vs "10kohm")
        if (intent.contains(QRegularExpression("\\d+[kKmMuU](?!\\w)"))) {
            explanation.reasons.append("Value notation may have multiple interpretations");
        }
        
        // Check historical rejection rate
        double historyRate = FeedbackStore::getApprovalRate(action.split(":").first());
        if (historyRate < 0.7) {
            explanation.reasons.append(QString("Similar actions have %.0f%% historical approval rate").arg(historyRate * 100));
        }
        
        if (explanation.reasons.isEmpty()) {
            explanation.reasons.append("Multiple valid interpretations exist");
        }
    }
    
    // Generate alternatives if confidence is low
    if (rawConfidence < 0.8) {
        // Placeholder: In production, NLParser would provide alternatives
        QJsonObject alt;
        alt["action"] = "Please clarify your intent";
        alt["confidence"] = 0.0;
        explanation.alternatives.append(alt);
    }
    
    return explanation;
}

// ============================================================================
// FailureLabeler Implementation
// ============================================================================

QList<FailureLabel> FailureLabeler::labelFailures(
    const QJsonObject& solverStats,
    const QJsonObject& netlistState
) {
    QList<FailureLabel> labels;
    
    // Check for convergence failure
    if (!solverStats["converged"].toBool(true)) {
        FailureLabel label;
        label.type = FailureType::CONVERGENCE_FAILED;
        label.severity = getFailureSeverity(label.type);
        label.iteration = solverStats["iterations"].toInt();
        label.residual = solverStats["residual"].toDouble();
        label.message = QString("Newton-Raphson failed after %1 iterations (residual: %2)")
                        .arg(label.iteration).arg(label.residual);
        labels.append(label);
    }
    
    // Check for GMIN fallback
    if (solverStats["gminSteppingUsed"].toBool(false)) {
        FailureLabel label;
        label.type = FailureType::GMIN_FALLBACK;
        label.severity = getFailureSeverity(label.type);
        label.message = "GMIN stepping was required for convergence";
        labels.append(label);
    }
    
    // Check for source stepping
    if (solverStats["sourceSteppingUsed"].toBool(false)) {
        FailureLabel label;
        label.type = FailureType::SOURCE_STEPPING;
        label.severity = getFailureSeverity(label.type);
        label.message = "Source ramping was required for convergence";
        labels.append(label);
    }
    
    // Check for NaN/overflow
    if (solverStats["error_detail"].toString().contains("NaN") ||
        solverStats["error_detail"].toString().contains("Inf")) {
        FailureLabel label;
        label.type = FailureType::OVERFLOW_DETECTED;
        label.severity = getFailureSeverity(label.type);
        label.message = "Numerical overflow detected in matrix operations";
        labels.append(label);
    }
    
    // Check for floating nodes in netlist
    QJsonArray ercViolations = netlistState["ercViolations"].toArray();
    for (const auto& viol : ercViolations) {
        QJsonObject v = viol.toObject();
        if (v["type"].toString() == "FLOATING_NODE") {
            FailureLabel label;
            label.type = FailureType::FLOATING_NODE;
            label.severity = getFailureSeverity(label.type);
            label.nodeOrComponent = v["node"].toString();
            label.message = QString("Node %1 is not connected to ground").arg(label.nodeOrComponent);
            labels.append(label);
        }
    }
    
    return labels;
}

} // namespace AIAPI
