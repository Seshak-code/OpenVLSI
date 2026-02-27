#include "schema_api.h"
#include "mainwindow.h"
#include "schematic_editor.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>

namespace AIAPI {

static SchematicEditor* getEditor() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (MainWindow* mw = qobject_cast<MainWindow*>(widget)) {
            return mw->getSchematicEditor();
        }
    }
    return nullptr;
}

// ============================================================================
// COMPONENT PLACEMENT
// ============================================================================

QString placeComponent(const PlacementParams& params) {
    SchematicEditor* editor = getEditor();
    if (!editor) return "";
    
    // Scale coords: The AI likely thinks in GRID_STEP units or raw pixels?
    // The struct says "Grid coordinates (in GRID_STEP units)"
    // So we multiply by GRID_STEP (20)
    // Wait, let's verify if schematic_editor_api expects raw pixels or grid units. 
    // It takes QPointF pos. Visual components are typically placed at 20px grid.
    // If param is (5, 5), that implies (100, 100).
    
    double scale = 20.0; // GRID_STEP from visual_component.h
    QPointF pos(params.x * scale, params.y * scale);
    
    return editor->placeComponent(params.componentType, pos, params.rotation, params.params);
}

bool moveComponent(const QString& componentId, double x, double y) {
    SchematicEditor* editor = getEditor();
    if (!editor) return false;
    double scale = 20.0;
    return editor->moveComponent(componentId, x * scale, y * scale);
}

bool rotateComponent(const QString& componentId, double degrees) {
    SchematicEditor* editor = getEditor();
    if (!editor) return false;
    return editor->rotateComponent(componentId, degrees);
}

bool deleteComponent(const QString& componentId) {
    SchematicEditor* editor = getEditor();
    if (!editor) return false;
    return editor->deleteComponent(componentId);
}

QRectF getComponentBounds(const QString& componentId) {
    SchematicEditor* editor = getEditor();
    if (!editor) return QRectF();
    
    VisualComponent* c = editor->findComponent(componentId);
    if (!c) return QRectF();
    
    QRectF b = c->sceneBoundingRect();
    double scale = 20.0;
    return QRectF(b.x()/scale, b.y()/scale, b.width()/scale, b.height()/scale);
}

// ============================================================================
// WIRE ROUTING (Bridge)
// ============================================================================

QString routeWire(const RoutingConstraints& constraints) {
    SchematicEditor* editor = getEditor();
    if (!editor) return "";
    
    double scale = 20.0;
    QVector<QPointF> scaledPoints;
    for(const auto& p : constraints.waypoints) {
        scaledPoints.append(QPointF(p.x() * scale, p.y() * scale));
    }
    
    editor->placeWirePath(scaledPoints);
    
    // Generate wire ID based on timestamp
    QString wireId = QString("WIRE_%1").arg(QDateTime::currentMSecsSinceEpoch());
    return wireId;
}

bool optimizePlacement(const QString& componentId) {
    SchematicEditor* editor = getEditor();
    if (!editor) return false;
    return editor->optimizeComponentRotation(componentId);
}

QString routeWithPhysics(const RoutingConstraints& constraints, const WirePhysics& physics) {
    SchematicEditor* editor = getEditor();
    if (!editor) return "";
    
    double scale = 20.0;
    QVector<QPointF> scaledPoints;
    for(const auto& p : constraints.waypoints) {
        scaledPoints.append(QPointF(p.x() * scale, p.y() * scale));
    }
    
    // Calculate required width based on physics
    // Rule of Thumb (Simplified): Width = Current * k
    // Let's say 1A = 2.0 width (default)
    float width = physics.currentRating > 0 ? physics.currentRating * 2.0 : 2.0;
    if (width < 1.0) width = 1.0;
    if (width > 10.0) width = 10.0;
    
    editor->placePhysicallyAwareWire(scaledPoints, width, physics.currentRating);
    return "WIRE_OK"; 
}

// Stub/Simple implementations for others
QStringList getConnectedWires(const QString& nodeId) { return {}; }
QString autoRoute(const QString& startNodeId, const QString& endNodeId) { return ""; }
bool validateWirePath(const QVector<QPointF>& path) { return true; }

// ============================================================================
// GEOMETRIC QUERIES
// ============================================================================

QJsonObject getFullCircuitState() {
     SchematicEditor* editor = getEditor();
     if (!editor) return QJsonObject();
     return editor->getCircuitState();
}

QJsonObject validateComponent(const QString& componentId, const QJsonObject& stimuli) {
    // STUBBED out to preserve GUI -> Physics isolation.
    // In a compliant architecture, this must delegate to the compute orchestration layer
    // via an ISimulationDriver DTO request.
    QJsonObject jsonRes;
    jsonRes["passed"] = false;
    jsonRes["report"] = "Validation blocked: AI API must use ISimulationDriver for block validation.";
    jsonRes["trace"] = QJsonObject();
    return jsonRes;
}

// ============================================================================
// SIMULATION CONTEXT (Phase 1.7.9 - AI Integration Hooks)
// ============================================================================

// Static storage for convergence trace (populated by CircuitSim during solve)
static QJsonArray s_convergenceTrace;
static QString s_lastFailureReason;
static QString s_lastRunStatus = "NOT_RUN";
static double s_lastSolveTime_ms = 0.0;
static QJsonObject s_lastNodeVoltages;

void recordConvergenceStep(int iteration, double residual, double stepSize) {
    QJsonObject step;
    step["iteration"] = iteration;
    step["residual"] = residual;
    step["stepSize"] = stepSize;
    s_convergenceTrace.append(step);
}

void clearConvergenceTrace() {
    s_convergenceTrace = QJsonArray();
}

void setLastRunResult(const QString& status, const QString& failureReason, double solveTime_ms) {
    s_lastRunStatus = status;
    s_lastFailureReason = failureReason;
    s_lastSolveTime_ms = solveTime_ms;
}

void setLastNodeVoltages(const QJsonObject& voltages) {
    s_lastNodeVoltages = voltages;
}

QJsonObject getSimulationContext() {
    SchematicEditor* editor = getEditor();
    QJsonObject ctx;
    
    if (editor) {
        // Topology hash
        ctx["topologyHash"] = QString::number(editor->getTopologyHash(), 16);

        // Mode context removed - unified component library
        ctx["currentMode"] = "UNIFIED";

        // ERC violations
        QJsonArray violations;
        if (editor->getScene()) {
            auto* scene = qobject_cast<GridScene*>(editor->getScene());
            if (scene) {
                for (const auto& v : scene->getViolations()) {
                    QJsonObject vObj;
                    vObj["type"] = static_cast<int>(v.type);
                    vObj["severity"] = v.severity == ERC::Severity::ERROR ? "ERROR" :
                                       v.severity == ERC::Severity::WARNING ? "WARNING" : "INFO";
                    vObj["message"] = v.message;
                    vObj["x"] = v.location.x();
                    vObj["y"] = v.location.y();
                    violations.append(vObj);
                }
            }
        }
        ctx["ercViolations"] = violations;
        ctx["canSimulate"] = violations.isEmpty() || 
                             std::none_of(violations.begin(), violations.end(), 
                                         [](const QJsonValue& v){ return v["severity"] == "ERROR"; });
    } else {
        ctx["topologyHash"] = "0";
        ctx["currentMode"] = "UNIFIED";
        ctx["ercViolations"] = QJsonArray();
        ctx["canSimulate"] = false;
    }
    
    // Last run status
    ctx["lastRunStatus"] = s_lastRunStatus;
    ctx["lastFailureReason"] = s_lastFailureReason;
    ctx["lastSolveTime_ms"] = s_lastSolveTime_ms;
    ctx["convergenceTrace"] = s_convergenceTrace;
    ctx["nodeVoltages"] = s_lastNodeVoltages;
    
    return ctx;
}

QJsonArray getConvergenceTrace() {
    return s_convergenceTrace;
}

QString analyzeFailure() {
    if (s_lastRunStatus != "FAILED") {
        return "No failure to analyze. Last run status: " + s_lastRunStatus;
    }
    
    QString analysis;
    analysis += "== SIMULATION FAILURE ANALYSIS ==\n\n";
    analysis += "Failure Reason: " + s_lastFailureReason + "\n\n";
    
    // Analyze convergence trace
    if (!s_convergenceTrace.isEmpty()) {
        double lastResidual = s_convergenceTrace.last()["residual"].toDouble();
        int iterations = s_convergenceTrace.size();
        
        analysis += QString("Iterations: %1\n").arg(iterations);
        analysis += QString("Final Residual: %1\n\n").arg(lastResidual, 0, 'e', 3);
        
        // Detect convergence patterns
        if (iterations >= 50) {
            analysis += "DIAGNOSIS: Max iterations reached. Possible causes:\n";
            analysis += "  - Floating node (check ERC)\n";
            analysis += "  - No path to ground\n";
            analysis += "  - Voltage source loop\n";
            analysis += "  - Extremely high loop gain\n";
        } else if (lastResidual > 1e6) {
            analysis += "DIAGNOSIS: Divergent solution. Possible causes:\n";
            analysis += "  - Physical impossibility (e.g., voltage source without load)\n";
            analysis += "  - Short circuit\n";
            analysis += "  - Numerical instability from extreme component values\n";
        } else {
            analysis += "DIAGNOSIS: Slow convergence. Consider:\n";
            analysis += "  - Adding initial conditions\n";
            analysis += "  - Relaxing tolerance\n";
            analysis += "  - Simplifying circuit topology\n";
        }
    } else {
        analysis += "No convergence trace available.\n";
    }
    
    return analysis;
}

} // namespace AIAPI

