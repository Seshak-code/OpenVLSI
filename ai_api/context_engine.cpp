
#include "context_engine.h"
#include "schema_api.h"
#include "crash_report.h"
#include "topology_analyzer.h"
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
    
    QString prompt = "You are an AI Copilot for BeaverSim - an Experimental Circuit Designer.\n";
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
    
    // 5. Include recent crash context if available (for debugging assistance)
    QString crashContext = DebugLog::loadLatestCrashContext();
    if (!crashContext.isEmpty()) {
        prompt += "\n=== RECENT CRASH/ERROR CONTEXT ===\n";
        prompt += crashContext;
        prompt += "\nAnalyze this crash context and consider it when assisting the user.\n";
    }
    
    // 6. Phase 1.26 Step 4: AI Context Representation Layer
    prompt += "\n=== STRUCTURED CONTEXT (Step 4) ===\n";
    
    // 4.1: Topology Chunks
    QJsonObject topology = serializeTopologyChunks();
    QJsonObject deviceTables = topology["deviceTables"].toObject();
    prompt += "Device Summary:\n";
    prompt += QString("  Resistors: %1, Capacitors: %2, Diodes: %3\n")
        .arg(deviceTables["resistors"].toArray().size())
        .arg(deviceTables["capacitors"].toArray().size())
        .arg(deviceTables["diodes"].toArray().size());
    prompt += QString("  BJTs: %1, MOSFETs: %2, Sources: %3\n")
        .arg(deviceTables["bjts"].toArray().size())
        .arg(deviceTables["mosfets"].toArray().size())
        .arg(deviceTables["sources"].toArray().size());
    prompt += QString("  DC Status: %1\n").arg(topology["dcOperatingPoint"].toObject()["status"].toString());
    
    // 4.2: Probe Context
    QJsonObject probes = serializeProbeContext();
    QJsonArray probeList = probes["probes"].toArray();
    if (!probeList.isEmpty()) {
        prompt += "Probes:\n";
        for (const auto& p : probeList) {
            QJsonObject po = p.toObject();
            prompt += QString("  - %1 (%2): %3\n")
                .arg(po["name"].toString())
                .arg(po["label"].toString())
                .arg(po["value"].toString());
        }
    }
    
    // 4.3: Solver Decisions
    QJsonObject solver = serializeSolverDecisions();
    prompt += "Solver Config:\n";
    prompt += QString("  Method: %1, LTE Threshold: %2\n")
        .arg(solver["integrationMethod"].toString())
        .arg(solver["lteThreshold"].toDouble());
    QJsonObject gmin = solver["gminStepping"].toObject();
    prompt += QString("  GMIN Active: %1, Current GMIN: %2\n")
        .arg(gmin["active"].toBool() ? "Yes" : "No")
        .arg(gmin["currentGmin"].toDouble());
    
    // 7. Phase 1.26 Step 5: Topology-Aware Prompt Compression
    prompt += "\n=== TOPOLOGY ANALYSIS (Step 5) ===\n";
    
    // 5.1: Functional Block Identification
    QJsonObject topologyAnalysis = TopologyAnalyzer::toJson();
    QJsonArray functionalBlocks = topologyAnalysis["functionalBlocks"].toArray();
    
    if (!functionalBlocks.isEmpty()) {
        prompt += "Identified Blocks:\n";
        for (const auto& b : functionalBlocks) {
            QJsonObject bo = b.toObject();
            prompt += QString("  - %1 (%2): %3\n")
                .arg(bo["name"].toString())
                .arg(bo["type"].toString())
                .arg(bo["description"].toString());
        }
    }
    
    // 5.2: Signal Pathway Summary
    QJsonArray signalPaths = topologyAnalysis["signalPaths"].toArray();
    if (!signalPaths.isEmpty()) {
        prompt += "Signal Paths:\n";
        for (const auto& p : signalPaths) {
            QJsonObject po = p.toObject();
            prompt += QString("  - %1: %2\n")
                .arg(po["id"].toString())
                .arg(po["summary"].toString());
        }
    }
    
    // Compressed summary (token-efficient)
    prompt += "\n[COMPRESSED]: " + topologyAnalysis["compressedPrompt"].toString() + "\n";
    
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

QJsonArray ContextEngine::analyzeSignalPaths() {
    QJsonArray paths;
    QJsonObject state = getContextJson();
    QJsonArray components = state["components"].toArray();
    
    // Identify sources and sinks
    QStringList sources;  // VoltageSource, CurrentSource positive terminals
    QStringList sinks;    // Ground, or components with "load" in name
    QMap<QString, QString> componentTypes;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString name = c["name"].toString();
        QString type = c["type"].toString();
        componentTypes[name] = type;
        
        if (type == "VoltageSource" || type == "CurrentSource") {
            sources.append(name);
        }
        if (type == "Ground") {
            sinks.append(name);
        }
    }
    
    // For each source, create a path description
    for (const QString& src : sources) {
        QJsonObject pathObj;
        pathObj["source"] = src;
        pathObj["sink"] = sinks.isEmpty() ? "Unknown" : sinks.first();
        
        // Build path by finding connected components
        QJsonArray pathComponents;
        pathComponents.append(src);
        
        // Simple heuristic: find BJTs/MOSFETs as amplification stages
        QJsonArray probePoints;
        for (const auto& val : components) {
            QJsonObject c = val.toObject();
            QString name = c["name"].toString();
            QString type = c["type"].toString();
            
            if (type == "Bjt" || type == "Mosfet") {
                pathComponents.append(name);
                // Recommend probing at transistor output
                QJsonObject probe;
                probe["node"] = name + ".C"; // Collector/Drain
                probe["label"] = "Vout_" + name;
                probePoints.append(probe);
            }
        }
        
        if (!sinks.isEmpty()) {
            pathComponents.append(sinks.first());
        }
        
        pathObj["path"] = pathComponents;
        pathObj["probePoints"] = probePoints;
        
        // Add input probe suggestion
        QJsonObject inputProbe;
        inputProbe["node"] = src + ".+";
        inputProbe["label"] = "Vin";
        
        QJsonArray allProbes;
        allProbes.append(inputProbe);
        for (const auto& p : probePoints) {
            allProbes.append(p);
        }
        pathObj["probePoints"] = allProbes;
        
        paths.append(pathObj);
    }
    
    return paths;
}

QJsonArray ContextEngine::getDebugSuggestions() {
    QJsonArray suggestions;
    QJsonObject state = getContextJson();
    QJsonArray components = state["components"].toArray();
    
    // Track which component types exist
    bool hasGround = false;
    bool hasSource = false;
    QStringList transistors;
    QStringList floatingNodes;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString name = c["name"].toString();
        QString type = c["type"].toString();
        
        if (type == "Ground") hasGround = true;
        if (type == "VoltageSource" || type == "CurrentSource") hasSource = true;
        if (type == "Bjt" || type == "Mosfet") transistors.append(name);
    }
    
    // Suggestion 1: No ground
    if (!hasGround) {
        QJsonObject s;
        s["node"] = "Circuit";
        s["reason"] = "No Ground node found. Add a Ground component for proper reference.";
        s["priority"] = 1;
        suggestions.append(s);
    }
    
    // Suggestion 2: No source
    if (!hasSource) {
        QJsonObject s;
        s["node"] = "Circuit";
        s["reason"] = "No power source found. Add a VoltageSource or CurrentSource.";
        s["priority"] = 1;
        suggestions.append(s);
    }
    
    // Suggestion 3: Probe at transistor inputs (high-gain stages)
    for (const QString& t : transistors) {
        QJsonObject s;
        s["node"] = t + ".B"; // Base/Gate
        s["reason"] = QString("Probe at %1 input (Base/Gate) - high-gain stage, sensitive to bias.").arg(t);
        s["priority"] = 2;
        suggestions.append(s);
        
        // Also suggest output
        QJsonObject sOut;
        sOut["node"] = t + ".C"; // Collector/Drain
        sOut["reason"] = QString("Probe at %1 output (Collector/Drain) - observe amplified signal.").arg(t);
        sOut["priority"] = 2;
        suggestions.append(sOut);
    }
    
    // Suggestion 4: For Darlington, suggest the inter-stage node
    if (transistors.size() >= 2) {
        QJsonObject s;
        s["node"] = transistors[0] + ".E → " + transistors[1] + ".B";
        s["reason"] = "Darlington inter-stage: Probe between Q1 emitter and Q2 base to verify current gain.";
        s["priority"] = 2;
        suggestions.append(s);
    }
    
    return suggestions;
}

// =============================================================================
// Phase 1.26 Step 4: AI Context Representation Layer
// =============================================================================

QJsonObject ContextEngine::serializeTopologyChunks() {
    QJsonObject result;
    QJsonObject state = getContextJson();
    QJsonArray components = state["components"].toArray();
    
    // 1. Device Tables (grouped by type)
    QJsonArray resistors, capacitors, diodes, bjts, mosfets, sources;
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString type = c["type"].toString();
        QString name = c["name"].toString();
        QJsonObject props = c["properties"].toObject();
        
        QJsonObject entry;
        entry["id"] = name;
        
        if (type == "Resistor") {
            entry["value"] = props["value"].toString();
            resistors.append(entry);
        } else if (type == "Capacitor") {
            entry["value"] = props["value"].toString();
            capacitors.append(entry);
        } else if (type == "Diode" || type == "SchottkyDiode") {
            entry["Is"] = props["Is"].toString();
            entry["N"] = props["N"].toString();
            diodes.append(entry);
        } else if (type == "Bjt") {
            entry["model"] = props["Model"].toString();
            entry["beta"] = props["Beta"].toString();
            bjts.append(entry);
        } else if (type == "Mosfet") {
            entry["model"] = props["Model"].toString();
            entry["W"] = props["W"].toString();
            entry["L"] = props["L"].toString();
            mosfets.append(entry);
        } else if (type == "VoltageSource" || type == "CurrentSource") {
            entry["value"] = props["value"].toString();
            sources.append(entry);
        }
    }
    
    QJsonObject deviceTables;
    deviceTables["resistors"] = resistors;
    deviceTables["capacitors"] = capacitors;
    deviceTables["diodes"] = diodes;
    deviceTables["bjts"] = bjts;
    deviceTables["mosfets"] = mosfets;
    deviceTables["sources"] = sources;
    result["deviceTables"] = deviceTables;
    
    // 2. DC Operating Point (Placeholder - would come from last simulation)
    // In a real implementation, this would read from SimulationTrace
    QJsonObject dcOP;
    dcOP["status"] = "Not yet simulated";
    dcOP["nodeVoltages"] = QJsonArray();
    result["dcOperatingPoint"] = dcOP;
    
    // 3. Transient Endpoints (Placeholder)
    QJsonObject transient;
    transient["status"] = "Not yet simulated";
    transient["finalTime"] = 0.0;
    result["transientEndpoint"] = transient;
    
    return result;
}

QJsonObject ContextEngine::serializeProbeContext() {
    QJsonObject result;
    QJsonObject state = getContextJson();
    QJsonArray components = state["components"].toArray();
    
    // 1. Named Probe Values
    QJsonArray probeValues;
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString type = c["type"].toString();
        if (type == "Probe") {
            QJsonObject p;
            p["name"] = c["name"].toString();
            p["label"] = c["properties"].toObject()["Label"].toString();
            // Value would come from simulation; placeholder
            p["value"] = "N/A";
            probeValues.append(p);
        }
    }
    result["probes"] = probeValues;
    
    // 2. Historical Errors (Placeholder - would come from trace history)
    QJsonArray errorHistory;
    // In production, this would iterate over last N simulation traces
    // and extract convergence failures, KCL residuals, etc.
    result["errorHistory"] = errorHistory;
    
    // 3. KCL Residual Summary
    QJsonObject kclSummary;
    kclSummary["maxResidual_A"] = "N/A";
    kclSummary["residualStatus"] = "Not yet computed";
    result["kclResiduals"] = kclSummary;
    
    return result;
}

QJsonObject ContextEngine::serializeSolverDecisions() {
    QJsonObject result;
    
    // 1. Integration Method
    // In production, this would query the active CircuitSim instance
    result["integrationMethod"] = "Trapezoidal"; // Default
    result["lteThreshold"] = 1e-4;
    result["maxIterations"] = 20;
    
    // 2. GMIN Stepping
    QJsonObject gminState;
    gminState["active"] = false;
    gminState["currentGmin"] = 1e-12;
    gminState["steppingPhase"] = "None";
    result["gminStepping"] = gminState;
    
    // 3. Rollback History
    QJsonArray rollbacks;
    // Would track timestep rollbacks due to LTE violations
    result["rollbackHistory"] = rollbacks;
    
    // 4. Solver Flags
    QJsonObject flags;
    flags["useSourceStepping"] = false;
    flags["useDampedNewton"] = true;
    flags["pnjlimEnabled"] = true;
    result["solverFlags"] = flags;
    
    return result;
}

} // namespace AIAPI

