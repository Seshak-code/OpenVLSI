// Test stubs for AI API functions that depend on the GUI layer.
// Provides minimal implementations so AI-layer tests can link without
// compiling mainwindow.cpp, schematic_editor.cpp, etc.

#include <optional>
#include "context_engine.h"
#include "schema_api.h"
#include "../analysis/topology_analyzer.h"
#include <QJsonArray>
#include <QJsonObject>

namespace AIAPI {

// --- ContextEngine stubs ---

AIPolicy ContextEngine::currentPolicy_ = AIPolicy::defaultPolicy();
std::optional<SimulationTrace> ContextEngine::lastTrace_ = std::nullopt;

QString ContextEngine::applyPolicy(const QString& aiResponse, const QJsonObject&) {
    return aiResponse;
}

QJsonObject ContextEngine::getContextJson() {
    QJsonObject state;
    state["components"] = QJsonArray();
    state["wires"] = QJsonArray();
    return state;
}

QString ContextEngine::generateSystemPrompt() {
    QString prompt = "You are an AI Copilot for BeaverSim.\n";
    prompt += "CURRENT CIRCUIT CONTEXT:\n";
    prompt += QString("Total Components: 0\n");

    QJsonObject topology = serializeTopologyChunks();
    QJsonObject probes = serializeProbeContext();
    QJsonObject solver = serializeSolverDecisions();
    Q_UNUSED(topology);
    Q_UNUSED(probes);
    prompt += QString("Solver: %1\n").arg(solver["integrationMethod"].toString());

    QJsonObject topologyAnalysis = TopologyAnalyzer::toJson();
    prompt += "\n[COMPRESSED]: " + topologyAnalysis["compressedPrompt"].toString() + "\n";

    return prompt;
}

QString ContextEngine::describeComponent(const QJsonObject& c) {
    return QString("- %1 (%2)").arg(c["name"].toString()).arg(c["type"].toString());
}

QString ContextEngine::inferTopologyIntent(const QJsonObject&) {
    return "General Analog/Mixed-Signal Circuit.";
}

QString ContextEngine::getSpatialContext() {
    return "Spatial context unavailable (test stub).";
}

QJsonArray ContextEngine::analyzeSignalPaths() {
    return QJsonArray();
}

QJsonArray ContextEngine::getDebugSuggestions() {
    return QJsonArray();
}

QJsonObject ContextEngine::serializeTopologyChunks() {
    QJsonObject result;
    QJsonObject deviceTables;
    deviceTables["resistors"] = QJsonArray();
    deviceTables["capacitors"] = QJsonArray();
    deviceTables["diodes"] = QJsonArray();
    deviceTables["bjts"] = QJsonArray();
    deviceTables["mosfets"] = QJsonArray();
    deviceTables["sources"] = QJsonArray();
    result["deviceTables"] = deviceTables;

    QJsonObject dcOP;
    if (lastTrace_.has_value() && !lastTrace_->voltageSeries.empty()) {
        dcOP["status"] = lastTrace_->isFailure() ? "Failed" : "Converged";
        QJsonArray nodeVoltages;
        for (auto it = lastTrace_->voltageSeries.begin(); it != lastTrace_->voltageSeries.end(); ++it) {
            if (!it.value().empty()) {
                QJsonObject nv;
                nv["node"] = it.key();
                nv["voltage"] = it.value().back();
                nodeVoltages.append(nv);
            }
        }
        dcOP["nodeVoltages"] = nodeVoltages;
    } else {
        dcOP["status"] = "Not yet simulated";
        dcOP["nodeVoltages"] = QJsonArray();
    }
    result["dcOperatingPoint"] = dcOP;

    QJsonObject transient;
    if (lastTrace_.has_value() && !lastTrace_->timePoints.empty()) {
        transient["status"] = "Completed";
        transient["finalTime"] = lastTrace_->timePoints.back();
    } else {
        transient["status"] = "Not yet simulated";
        transient["finalTime"] = 0.0;
    }
    result["transientEndpoint"] = transient;

    return result;
}

QJsonObject ContextEngine::serializeProbeContext() {
    QJsonObject result;
    result["probes"] = QJsonArray();

    QJsonArray errorHistory;
    if (lastTrace_.has_value()) {
        for (const auto& failure : lastTrace_->failureModes) {
            QJsonObject err;
            err["type"] = failureTypeToString(failure.type);
            err["message"] = failure.message;
            err["node"] = failure.nodeOrComponent;
            err["timestamp"] = failure.timestamp;
            errorHistory.append(err);
        }
    }
    result["errorHistory"] = errorHistory;

    QJsonObject kclSummary;
    if (lastTrace_.has_value() && !lastTrace_->failureModes.isEmpty()) {
        double maxResidual = 0.0;
        for (const auto& f : lastTrace_->failureModes) {
            if (f.residual > maxResidual) maxResidual = f.residual;
        }
        kclSummary["maxResidual_A"] = QString::number(maxResidual, 'e', 3);
        kclSummary["residualStatus"] = lastTrace_->isFailure() ? "Above threshold" : "Within tolerance";
    } else {
        kclSummary["maxResidual_A"] = "N/A";
        kclSummary["residualStatus"] = lastTrace_.has_value() ? "Within tolerance" : "Not yet computed";
    }
    result["kclResiduals"] = kclSummary;

    return result;
}

QJsonObject ContextEngine::serializeSolverDecisions() {
    QJsonObject result;
    if (lastTrace_.has_value()) {
        const auto& config = lastTrace_->configParams;
        result["integrationMethod"] = config.solverType;
        result["lteThreshold"] = config.reltol;
        result["maxIterations"] = 20;
        QJsonObject gminState;
        gminState["active"] = config.gminSteppingEnabled;
        gminState["currentGmin"] = 1e-12;
        gminState["steppingPhase"] = config.gminSteppingEnabled ? "Active" : "None";
        result["gminStepping"] = gminState;
    } else {
        result["integrationMethod"] = "Trapezoidal";
        result["lteThreshold"] = 1e-4;
        result["maxIterations"] = 20;
        QJsonObject gminState;
        gminState["active"] = false;
        gminState["currentGmin"] = 1e-12;
        gminState["steppingPhase"] = "None";
        result["gminStepping"] = gminState;
    }
    QJsonArray rollbacks;
    result["rollbackHistory"] = rollbacks;
    QJsonObject flags;
    flags["useSourceStepping"] = false;
    flags["useDampedNewton"] = true;
    flags["pnjlimEnabled"] = true;
    result["solverFlags"] = flags;
    return result;
}

// --- schema_api stubs ---

QJsonObject getFullCircuitState() {
    QJsonObject state;
    state["components"] = QJsonArray();
    state["wires"] = QJsonArray();
    return state;
}

QRectF getComponentBounds(const QString&) {
    return QRectF();
}

QString placeComponent(const PlacementParams&) { return "stub_comp"; }
bool moveComponent(const QString&, double, double) { return true; }
bool rotateComponent(const QString&, double) { return true; }
bool deleteComponent(const QString&) { return true; }
QString routeWire(const RoutingConstraints&) { return "stub_wire"; }
QString routeWithPhysics(const RoutingConstraints&, const WirePhysics&) { return "stub_wire"; }
bool optimizePlacement(const QString&) { return true; }

QJsonObject validateComponent(const QString&, const QJsonObject&) {
    QJsonObject result;
    result["valid"] = true;
    result["violations"] = QJsonArray();
    return result;
}

QJsonObject getSimulationContext() { return QJsonObject(); }
QJsonArray getConvergenceTrace() { return QJsonArray(); }
QString analyzeFailure() { return ""; }
QString autoRoute(const QString&, const QString&) { return ""; }
bool validateWirePath(const QVector<QPointF>&) { return true; }
QStringList getConnectedWires(const QString&) { return {}; }
QStringList getComponentsInRegion(const QRectF&) { return {}; }
bool checkIntersection(const QString&, const QString&) { return false; }
QString findNearestComponent(const QPointF&, double) { return ""; }
QPointF snapToGrid(const QPointF& p) { return p; }
QString createBlock(const QString&, const QJsonObject&) { return ""; }
QString instantiateBlock(const QString&, const QString&, double, double) { return ""; }
QJsonObject getHierarchyTree() { return QJsonObject(); }
QString addShape(const QString&, const QVector<QPointF>&) { return ""; }
QVector<Shape3D> getShapesOnLayer(const QString&) { return {}; }
bool checkVerticalIntersection(const QString&, const QString&) { return false; }
QVector<DRCViolation> runDRC(const QVector<DRCRule>&) { return {}; }
QString suggestDRCFix(const DRCViolation&) { return ""; }
QJsonObject generateFloorplan(const QVector<FloorplanConstraint>&) { return QJsonObject(); }
double estimateWirelength(const QJsonObject&) { return 0.0; }
bool exportGDS(const QString&, const QJsonObject&) { return true; }
QString exportSPICE() { return ""; }
bool importLEF(const QString&) { return true; }
void logAUDIT(const AIQueryResult&) {}

} // namespace AIAPI
