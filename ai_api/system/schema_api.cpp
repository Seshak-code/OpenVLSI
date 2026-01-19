#include "schema_api.h"
#include "mainwindow.h"
#include "schematic_editor.h"
#include "../simulation/auto_harness.h"
#include <QApplication>
#include "schematic_editor.h"
#include <QApplication>
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
    editor->placeWirePath(scaledPoints);
    return "WIRE_OK"; // TODO: Return actual wire IDs
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
    // Convert JSON inputs to AutoValidator structs
    QVector<AutoValidator::Stimulus> stimVec;
    QJsonArray stimArr = stimuli["stimuli"].toArray();
    for(const auto& val : stimArr) {
        QJsonObject s = val.toObject();
        AutoValidator::Stimulus stim;
        stim.portName = s["port"].toString();
        stim.type = s["type"].toString();
        QJsonObject p = s["params"].toObject();
        for(auto it = p.begin(); it != p.end(); ++it) {
            stim.params[it.key()] = it.value().toDouble();
        }
        stimVec.append(stim);
    }
    
    // Get Block Definition (Normally we'd fetch from Library, assuming passed here for now or fetching from Editor)
    // For this prototype, we'll assume we can get it from the component instance or it's passed in stimuli for now.
    // In a real flow: ComponentLibrary::instance()->getComponentDef(componentId);
    // Let's create a temporary connection.
    
    AutoValidator validator;
    QJsonObject blockDef; 
    
    // If validating current canvas, fetch state from editor
    if (componentId == "CurrentCanvas") {
        if (SchematicEditor* editor = getEditor()) {
            blockDef = editor->getCircuitState();
        }
    } else {
        blockDef["components"] = QJsonArray(); 
    }
    
    auto result = validator.validateBlock(blockDef, stimVec);
    
    QJsonObject jsonRes;
    jsonRes["passed"] = result.passed;
    jsonRes["report"] = result.report;
    jsonRes["trace"] = result.traceData;
    return jsonRes;
}

} // namespace AIAPI
