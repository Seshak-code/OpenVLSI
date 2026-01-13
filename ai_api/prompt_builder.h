#pragma once
#include <QString>
#include <QJsonObject>
#include <QList>

namespace AIAPI {

/**
 * Structural container for the deterministic "Ground Truth" analysis
 * that feeds into the LLM Reasoning step.
 */
struct AlgoAnalysis {
    QString topologyHash;
    QString signalChain;        // e.g., "In -> Q1_Base -> Q1_Coll -> Out"
    QStringList drcFlags;       // e.g., "Node 5 Floating", "Violates MinWidth"
    QJsonObject opPoint;        // DC Node Voltages / Branch Currents
    QJsonObject sensitivities;  // dV_node / dParam (from dual.h)
    
    // For Complexity Arbiter
    int tokenDensity = 0;
    bool requiresDecomposition = false;
};

class PromptBuilder {
public:
    /**
     * Constructs the full "What-If" System Prompt for the LLM.
     * 
     * @param circuitState   JSON dump of the current circuit (Netlist + Meta).
     * @param userAction     The textual description of the proposed change.
     * @param targetCompId   ID of the component being modified.
     * @param analysis       Pre-computed algorithmic ground truth.
     */
    static QString buildWhatIfPrompt(const QJsonObject& circuitState, 
                                    const QString& userAction,
                                    const QString& targetCompId,
                                    const AlgoAnalysis& analysis);

    /**
     * Generates a "Multi-Step Plan" prompt for the Agentic Workflow.
     * Used when the request is complex (Optimization/Cost Reduction).
     */
    static QString buildAgenticPlanPrompt(const QString& userRequest,
                                         const AlgoAnalysis& analysis);

private:
    static QString generateGroundTruthBlock(const QJsonObject& state, const AlgoAnalysis& analysis);
    static QString generateReasoningKernels(const QString& targetId, const AlgoAnalysis& analysis);
    static QString formatOutputTemplate();
    static QString formatHybridConstraints(); // "Do not hallucinate connectivity..."
};

} // namespace AIAPI
