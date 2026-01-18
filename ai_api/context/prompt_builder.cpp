#include "prompt_builder.h"
#include <QJsonArray>
#include <QStringList>

namespace AIAPI {

QString PromptBuilder::buildWhatIfPrompt(const QJsonObject& circuitState, 
                                         const QString& userAction,
                                         const QString& targetCompId,
                                         const AlgoAnalysis& analysis) 
{
    QString prompt = "You are AcuteSim-9000, a high-precision circuit reasoning engine.\n";
    prompt += "Your goal is to predict the physical consequences of modifying a circuit composed of linear and non-linear devices.\n\n";

    // 1. Hybrid Constraints (The "Constitution")
    prompt += formatHybridConstraints();

    // 2. Ground Truth (The "Facts")
    prompt += generateGroundTruthBlock(circuitState, analysis);

    // 3. The Proposed Change
    prompt += "\n### THE PROPOSED CHANGE\n";
    prompt += "User Action: " + userAction + "\n";
    prompt += "Target Component: " + targetCompId + "\n";

    // 4. Reasoning Kernels (The "Thinking Process")
    prompt += generateReasoningKernels(targetCompId, analysis);

    // 5. Output Format
    prompt += formatOutputTemplate();

    return prompt;
}

QString PromptBuilder::buildAgenticPlanPrompt(const QString& userRequest, const AlgoAnalysis& analysis)
{
    QString prompt = "You are AcuteSim-9000, functioning as a Lead Analog Designer.\n";
    prompt += "Your goal is to create a step-by-step implementation plan to achieve the user's objective.\n\n";
    
    prompt += "### OBJECTIVE\n" + userRequest + "\n\n";
    
    prompt += "### CIRCUIT CONTEXT\n";
    prompt += "Topology Hash: " + analysis.topologyHash + "\n";
    prompt += "Signal Chain: " + analysis.signalChain + "\n";
    prompt += "DRC Status: " + analysis.drcFlags.join(", ") + "\n";

    prompt += "\n### PLANNING PROTOCOL\n";
    prompt += "1. Break the user request into atomic, verifiable steps.\n";
    prompt += "2. Each step must be a specific circuit modification (Component Value change, Replacement).\n";
    prompt += "3. Order steps logically (e.g. Bias -> Gain -> Frequency Response).\n";
    
    prompt += "\n### OUTPUT FORMAT (Markdown List)\n";
    prompt += "- [ ] **Step 1**: [Action] (Reasoning)\n";
    prompt += "- [ ] **Step 2**: [Action] (Reasoning)\n";
    
    return prompt;
}

QString PromptBuilder::generateGroundTruthBlock(const QJsonObject& state, const AlgoAnalysis& analysis)
{
    QString block = "\n### THE CIRCUIT (Ground Truth)\n";
    block += "- Topology Hash: " + analysis.topologyHash + "\n";
    
    if (!analysis.signalChain.isEmpty()) {
        block += "- Signal Chain (Verified): " + analysis.signalChain + "\n";
    }
    
    if (!analysis.drcFlags.isEmpty()) {
        block += "- DRC Flags: " + analysis.drcFlags.join("; ") + "\n";
    } else {
        block += "- DRC Flags: PASS\n";
    }

    // Node Voltages
    block += "- Key Node Voltages (DC Bias):\n";
    QJsonObject op = analysis.opPoint;
    for(auto it = op.begin(); it != op.end(); ++it) {
        block += QString("  - Node %1: %2 V\n").arg(it.key()).arg(it.value().toString());
    }

    // Component List (Brief)
    block += "- Active Components:\n";
    QJsonArray components = state["components"].toArray();
    for(const auto& val : components) {
        QJsonObject comp = val.toObject();
        QString type = comp["type"].toString();
        // Only list active or key passives? For now list all for context
        block += QString("  - %1 (%2) %3\n")
                 .arg(comp["name"].toString())
                 .arg(type)
                 .arg(comp["properties"].toObject()["value"].toString());
    }

    return block;
}

QString PromptBuilder::generateReasoningKernels(const QString& targetId, const AlgoAnalysis& analysis)
{
    QString kernels = "\n### REASONING PROTOCOL\n";
    kernels += "Follow these steps strictly:\n";
    
    kernels += "1. **Identify the Sub-Circuit**: List all nodes and components directly connected to " + targetId + ".\n";
    
    kernels += "2. **Apply Local Physics**:\n";
    kernels += "   - If Resistance increases, Voltage Drop (V=IR) increases (assuming constant I) OR Current reduces (assuming constant V).\n";
    
    // Inject Sensitivity Hint if available
    if (analysis.sensitivities.contains(targetId)) {
        double sens = analysis.sensitivities[targetId].toDouble();
        kernels += QString("   - **Sensitivity Hint**: dV_out/d%1 ~= %2. Use this as a strong directional guide.\n")
                   .arg(targetId).arg(sens);
    }

    kernels += "3. **Trace the Signal Chain**:\n";
    kernels += "   - Follow the signal path forward using the 'Signal Chain' Ground Truth.\n";
    kernels += "   - Does this node drive a Gate/Base?\n";
    kernels += "   - If driving a Transistor, check the new V_gs/V_be against the Threshold/Turn-on voltage.\n";
    
    kernels += "4. **Check Constraints**:\n";
    kernels += "   - Will this push a Transistor into Saturation/Cutoff?\n";
    kernels += "   - Will this exceed a supply rail?\n";

    return kernels;
}

QString PromptBuilder::formatHybridConstraints()
{
    QString constraints = "### HYBRID ARCHITECTURE CONSTRAINTS\n";
    constraints += "- Use the provided 'Ground Truth' as absolute fact.\n";
    constraints += "- Do NOT attempt to infer connectivity from component names; use the explicitly provided adjacency.\n";
    constraints += "- Do NOT hallucinate signal paths; rely on the 'Signal Chain' provided.\n";
    constraints += "- If the 'DRC Flags' indicate an error, prioritize fixing it.\n";
    return constraints;
}

QString PromptBuilder::formatOutputTemplate()
{
    QString tmpl = "\n### OUTPUT\n";
    tmpl += "Provide a concise summary of the expected outcome.\n";
    tmpl += "- **Primary Effect**: (e.g., \"Output Gain will decrease by ~10dB\")\n";
    tmpl += "- **Side Effects**: (e.g., \"Bias point at Node 3 will shift up, potentially clipping the signal.\")\n";
    tmpl += "- **Confidence**: (High/Medium/Low) based on linearity of the local circuit.\n";
    return tmpl;
}

} // namespace AIAPI
