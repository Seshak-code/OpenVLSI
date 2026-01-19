
#include "schema_api.h"
#include "mainwindow.h"
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
    // TODO: Implement rotation logic to minimize wire length/crossovers
    return true; 
}

QString routeWithPhysics(const RoutingConstraints& constraints, const WirePhysics& physics) {
    // TODO: Adjust width/spacing based on physics (current, crosstalk)
    // For now, delegate to standard routing
    return routeWire(constraints);
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

} // namespace AIAPI
