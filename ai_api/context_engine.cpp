
#include "context_engine.h"
#include "schema_api.h"
#include <QJsonArray>
#include <QStringList>

namespace AIAPI {

QJsonObject ContextEngine::getContextJson() {
    return AIAPI::getFullCircuitState();
}

QString ContextEngine::generateSystemPrompt() {
    QJsonObject state = getContextJson();
    QJsonArray components = state["components"].toArray();
    QJsonArray wires = state["wires"].toArray();
    
    QString prompt = "You are an AI Copilot for a Tensor-Native EDA Tool.\n";
    prompt += "CURRENT CIRCUIT CONTEXT:\n";
    prompt += QString("Total Components: %1\n").arg(components.size());
    
    // 1. List Components
    prompt += "Components:\n";
    for(const auto& val : components) {
        prompt += describeComponent(val.toObject()) + "\n";
    }
    
    // 2. Connectivity (Simplified)
    // Ideally we would run generateNetlist() to get distinct nodes, 
    // but here we can just list wire endpoints or rely on the components' spatial location if connectivity info is missing in JSON
    // The current getCircuitState returns coords.
    // Future improvement: Include "Node ID" in the component JSON by running the netlister before export.
    
    prompt += "\nInferred Topology: " + inferTopologyIntent(state) + "\n";
    
    return prompt;
}

QString ContextEngine::describeComponent(const QJsonObject& c) {
    QString name = c["name"].toString();
    QString type = c["type"].toString();
    QJsonObject props = c["properties"].toObject();
    
    QString desc = QString("- %1 (%2)").arg(name).arg(type);
    
    if (props.contains("value")) desc += QString(", Value: %1").arg(props["value"].toString());
    
    // Detailed params
    QStringList details;
    for(auto it = props.begin(); it != props.end(); ++it) {
        if (it.key() != "value") {
            details << QString("%1=%2").arg(it.key()).arg(it.value().toString());
        }
    }
    if (!details.isEmpty()) {
        desc += " [" + details.join(", ") + "]";
    }
    
    // Location context (Top-Left, Center, etc)?
    // desc += QString(" at (%1, %2)").arg(c["x"].toInt()).arg(c["y"].toInt()); 
    
    return desc;
}

QString ContextEngine::inferTopologyIntent(const QJsonObject& state) {
    // Basic heuristics
    QJsonArray components = state["components"].toArray();
    int nmosCount = 0;
    int pmosCount = 0;
    bool hasDiffPair = false;
    
    for(const auto& val : components) {
        QJsonObject c = val.toObject();
        QString type = c["type"].toString();
        if (type == "Mosfet") {
            QJsonObject props = c["properties"].toObject();
            if (props["Model"].toString().startsWith("n")) nmosCount++;
            if (props["Model"].toString().startsWith("p")) pmosCount++;
        }
    }
    
    QString intent = "General Analog/Mixed-Signal Circuit.";
    if (nmosCount >= 2 && pmosCount >= 1) intent = "Likely an Amplifier or Logic Gate.";
    if (nmosCount == pmosCount && nmosCount > 0) intent = "CMOS Logic or Transmission Gate.";
    
    return intent;
}

} // namespace AIAPI
