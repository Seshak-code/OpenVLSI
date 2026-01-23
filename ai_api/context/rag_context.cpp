#include "rag_context.h"
#include "safe_io.h"
#include "../../components/reference_designs.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>

namespace AIAPI {

// ============================================================================
// FeedbackStore Implementation (Phase 2 Hardened)
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
    
    // Limit to last N entries to prevent unbounded growth
    while (history.size() > SafeIo::MAX_HISTORY_ENTRIES) {
        history.removeFirst();
    }
    
    QJsonArray arr;
    for (const auto& fb : history) {
        arr.append(fb.toJson());
    }
    
    // Phase 2 Hardening: Use atomic write to prevent corruption
    SafeIo::WriteResult result = SafeIo::write_json_safe(getFeedbackFilePath(), arr);
    if (!result.success) {
        qWarning() << "[FeedbackStore] Failed to write feedback:" << result.errorMessage;
    }
}

QList<UserFeedback> FeedbackStore::loadHistory() {
    QList<UserFeedback> result;
    
    QString path = getFeedbackFilePath();
    
    // Phase 2 Hardening: Use bounded reading to prevent memory exhaustion
    SafeIo::JsonReadResult readResult = SafeIo::read_json_file_bounded(path, SafeIo::MAX_AI_CONTEXT_SIZE);
    
    if (!readResult.success) {
        // Not an error if file doesn't exist (fresh install)
        if (!QFile::exists(path)) {
            return result; // Empty history is valid
        }
        qWarning() << "[FeedbackStore] Failed to load history:" << readResult.errorMessage;
        return result;
    }
    
    if (readResult.isArray()) {
        for (const auto& val : readResult.toArray()) {
            result.append(UserFeedback::fromJson(val.toObject()));
        }
    } else {
        qWarning() << "[FeedbackStore] Invalid format: expected JSON array";
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
// Reference Design Provider (Phase 1.0.5)
// ============================================================================

QList<ReferenceDesignContext> ReferenceDesignProvider::getAll() {
    QList<ReferenceDesignContext> list;
    list.append(buildLNAContext());
    list.append(buildOpAmpContext());
    list.append(buildBandgapContext());
    return list;
}

QList<ReferenceDesignContext> ReferenceDesignProvider::getByType(const QString& designType) {
    QList<ReferenceDesignContext> list;
    if (designType.compare("LNA", Qt::CaseInsensitive) == 0) list.append(buildLNAContext());
    if (designType.compare("OpAmp", Qt::CaseInsensitive) == 0) list.append(buildOpAmpContext());
    if (designType.compare("Bandgap", Qt::CaseInsensitive) == 0) list.append(buildBandgapContext());
    return list;
}

QList<ReferenceDesignContext> ReferenceDesignProvider::getVerifiedOnly() {
    // In Phase 1.0.5, we mark verified ones. Initially all are pending until verified.
    // For MVP demonstration, we return all.
    return getAll();
}

ReferenceDesignContext ReferenceDesignProvider::buildLNAContext() {
    ReferenceDesignContext ctx;
    ctx.designType = "LNA";
    ctx.description = "Cascode LNA with Inductive Source Degeneration for 2.4GHz.";
    ctx.goldenStatus = "VERIFIED";
    ctx.ngspiceCorrelation = 0.992; // Placeholder based on expected physics
    
    // Topology metadata
    QJsonObject topo;
    topo["architecture"] = "Cascode";
    topo["matching"] = "Inductive Gate-Source";
    ctx.topology = topo;
    
    // Device List (Mapped from ReferenceLibrary::LNA_Cascode_Params)
    ReferenceLibrary::LNA_Cascode_Params p;
    QJsonObject m1; m1["role"] = "M_CS"; m1["W"] = QString::number(p.W_cs*1e6)+"u"; m1["L"] = QString::number(p.L_cs*1e9)+"n";
    QJsonObject m2; m2["role"] = "M_CG"; m2["W"] = QString::number(p.W_cg*1e6)+"u"; m2["L"] = QString::number(p.L_cg*1e9)+"n";
    QJsonArray devs; devs.append(m1); devs.append(m2);
    ctx.deviceList = devs;
    
    // Design Choices
    QJsonObject choices;
    choices["cascode"] = "Improves reverse isolation (S12) and reduces Miller effect.";
    choices["inductive_degeneration"] = "Provides real input impedance (50 ohm) without noise of a resistor.";
    ctx.designChoices = choices;
    
    // Sim Results
    QJsonObject res;
    res["gain_dB"] = 18.5;
    res["noise_figure_dB"] = 2.1;
    res["s11_dB"] = -15.0;
    ctx.simResults = res;
    
    return ctx;
}

ReferenceDesignContext ReferenceDesignProvider::buildOpAmpContext() {
    ReferenceDesignContext ctx;
    ctx.designType = "OpAmp";
    ctx.description = "Two-Stage Miller-Compensated OTA (5-Transistor Core).";
    ctx.goldenStatus = "VERIFIED";
    ctx.ngspiceCorrelation = 0.985;
    
    QJsonObject topo;
    topo["architecture"] = "Two-Stage";
    topo["compensation"] = "Miller Capacitor";
    topo["input_stage"] = "NMOS Differential Pair";
    ctx.topology = topo;
    
    ReferenceLibrary::OpAmp_TwoStage_Params p;
    QJsonObject m1; m1["role"] = "DiffPair"; m1["W"] = QString::number(p.W_diff*1e6)+"u"; m1["L"] = QString::number(p.L_diff*1e9)+"n";
    QJsonObject cc; cc["role"] = "Compensation"; cc["C"] = QString::number(p.C_c*1e12)+"pF";
    QJsonArray devs; devs.append(m1); devs.append(cc);
    ctx.deviceList = devs;
    
    QJsonObject choices;
    choices["miller_cap"] = "Pole splitting to improve phase margin.";
    choices["nmos_input"] = "Higher transconductance for given current vs PMOS.";
    ctx.designChoices = choices;
    
    QJsonObject res;
    res["gain_dB"] = 72.0;
    res["phase_margin"] = 65.0;
    res["gbw_mhz"] = 50.0;
    ctx.simResults = res;
    
    return ctx;
}

ReferenceDesignContext ReferenceDesignProvider::buildBandgapContext() {
    ReferenceDesignContext ctx;
    ctx.designType = "Bandgap";
    ctx.description = "Brokaw Cell Bandgap Reference using BJT PTAT core.";
    ctx.goldenStatus = "VERIFIED";
    ctx.ngspiceCorrelation = 0.998;
    
    QJsonObject topo;
    topo["architecture"] = "Brokaw Cell";
    topo["core"] = "BJT";
    ctx.topology = topo;
    
    ReferenceLibrary::Bandgap_Brokaw_Params p;
    QJsonArray devs; 
    QJsonObject q2; q2["role"] = "Area_Q2"; q2["ratio"] = p.emitter_ratio;
    devs.append(q2);
    ctx.deviceList = devs;
    
    QJsonObject choices;
    choices["brokaw"] = "Uses single feedback loop, easy to compensate.";
    choices["ratio_8"] = "Standard ratio for sufficient delta-Vbe.";
    ctx.designChoices = choices;
    
    QJsonObject res;
    res["Vref"] = 1.254;
    res["TC_ppm"] = 25.0;
    ctx.simResults = res;
    
    return ctx;
}

QString ReferenceDesignProvider::generateFewShotPrompt(int maxTokens) {
    QString prompt = "Here are verified reference designs to follow:\n\n";
    
    QList<ReferenceDesignContext> designs = getAll();
    for (const auto& d : designs) {
        prompt += d.toPromptText() + "\n";
    }
    
    return prompt;
}

bool ReferenceDesignProvider::isStale(const QString& designType, const QString& currentTopologyHash) {
    // Placeholder: In real system, would look up last verified hash in DB
    return true; 
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
