
#include "context_engine.h"
#include "schema_api.h"
#include "mainwindow.h"
#include "schematic_editor.h"
#include <QJsonArray>
#include <QStringList>
#include <QApplication>

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
    
    // 2. Spatial Relationships (Phase 1.3)
    prompt += "\nSPATIAL LAYOUT:\n";
    prompt += getSpatialContext() + "\n";
    
    // 3. Inferred Topology
    prompt += "Inferred Topology: " + inferTopologyIntent(state) + "\n";
    
    // 4. Relational Placement Instructions
    prompt += "\nPLACEMENT GUIDE:\n";
    prompt += "- Use 'placeNextTo(type, refId, direction)' for relative placement.\n";
    prompt += "- Directions: Right, Left, Above, Below\n";
    prompt += "- Canvas uses a 20px grid. Bounding boxes are ~60x60.\n";
    prompt += "- If a placement fails, the area is likely occupied.\n";
    
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

QString ContextEngine::getSpatialContext() {
    // Delegate to SchematicEditor via schema_api
    // This gets the spatial relationships from the editor
    SchematicEditor* editor = nullptr;
    
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (MainWindow* mw = qobject_cast<MainWindow*>(widget)) {
            editor = mw->getSchematicEditor();
            break;
        }
    }
    
    if (editor) {
        return editor->getSpatialContext();
    }
    return "Spatial context unavailable.";
}

} // namespace AIAPI
