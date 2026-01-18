#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <vector>

/**
 * AI API: Schematic & Layout Manipulation
 * 
 * This API provides complete geometric and hierarchical access for GenAI agents
 * to perform RTL-to-GDS automation, including:
 * - Component placement with precise coordinates
 * - Wire routing with collision detection
 * - 3D layout manipulation (future)
 * - Geometric queries (bounding boxes, intersections, DRC)
 */

namespace AIAPI {

// ============================================================================
// COMPONENT PLACEMENT
// ============================================================================

struct PlacementParams {
    QString componentType;  // "Resistor", "NMOS", "Inverter", etc.
    double x, y;            // Grid coordinates (in GRID_STEP units)
    double rotation;        // 0, 90, 180, 270 degrees
    QJsonObject params;     // {"resistance": "10k", "W": "2u", "L": "180n"}
};

/**
 * Place a component at specified coordinates.
 * Returns: Component ID (unique identifier)
 */
QString placeComponent(const PlacementParams& params);

/**
 * Move an existing component.
 */
bool moveComponent(const QString& componentId, double x, double y);

/**
 * Rotate a component.
 */
bool rotateComponent(const QString& componentId, double degrees);

/**
 * Delete a component.
 */
bool deleteComponent(const QString& componentId);

/**
 * Get component bounding box (for collision detection).
 */
QRectF getComponentBounds(const QString& componentId);

// ============================================================================
// WIRE ROUTING
// ============================================================================

struct RoutingConstraints {
    QString netName;
    QVector<QPointF> waypoints;  // Ordered routing points
    double minSpacing;           // Minimum spacing to other wires (DRC)
    bool allowVias;              // For multi-layer routing (future)
};

/**
 * Route a wire with collision avoidance.
 * Returns: Wire ID or empty string if routing fails
 */
QString routeWire(const RoutingConstraints& constraints);

/**
 * Auto-route between two nodes using A* pathfinding.
 */
QString autoRoute(const QString& startNodeId, const QString& endNodeId);

/**
 * Check if a wire path is valid (no DRC violations).
 */
bool validateWirePath(const QVector<QPointF>& path);

/**
 * Get all wires connected to a node.
 */
QStringList getConnectedWires(const QString& nodeId);

// ============================================================================
// GEOMETRIC QUERIES
// ============================================================================

/**
 * Get all components within a bounding box.
 */
QStringList getComponentsInRegion(const QRectF& region);

/**
 * Check for intersection between two components.
 */
bool checkIntersection(const QString& comp1, const QString& comp2);

/**
 * Get the closest component to a point.
 */
QString findNearestComponent(const QPointF& point, double maxDistance);

/**
 * Get grid-snapped coordinate.
 */
QPointF snapToGrid(const QPointF& point);

// ============================================================================
// HIERARCHICAL DESIGN
// ============================================================================

struct HierarchyNode {
    QString blockName;
    QString instanceName;
    QVector<QString> children;  // Sub-blocks
    QJsonObject ports;          // {"VDD": 1, "GND": 0, "IN": 2}
};

/**
 * Create a hierarchical block (subcircuit).
 */
QString createBlock(const QString& blockName, const QJsonObject& definition);

/**
 * Instantiate a block.
 */
QString instantiateBlock(const QString& blockName, const QString& instanceName, 
                         double x, double y);

/**
 * Get hierarchy tree (for floorplanning).
 */
QJsonObject getHierarchyTree();

// ============================================================================
// 3D LAYOUT (Future - Phase 5+)
// ============================================================================

struct Layer {
    QString name;      // "Metal1", "Poly", "Diffusion"
    double zIndex;     // Vertical stacking order
    QColor color;      // Visualization color
};

struct Shape3D {
    QString shapeId;
    QString layerName;
    QVector<QPointF> polygon;  // 2D footprint
    double height;             // Z-axis thickness
};

/**
 * Add a shape to a specific layer.
 */
QString addShape(const QString& layerName, const QVector<QPointF>& polygon);

/**
 * Get all shapes on a layer.
 */
QVector<Shape3D> getShapesOnLayer(const QString& layerName);

/**
 * Check vertical intersection (3D DRC).
 */
bool checkVerticalIntersection(const QString& shape1, const QString& shape2);

// ============================================================================
// DRC (Design Rule Check)
// ============================================================================

struct DRCRule {
    QString ruleName;
    QString layer1, layer2;
    double minSpacing;
    double minWidth;
    double minEnclosure;
};

struct DensityRule {
    QString layerName;
    double minDensity;  // e.g., 0.3 (30%)
    double maxDensity;  // e.g., 0.7 (70%)
    double windowSize;  // e.g., 100um x 100um check window
};

struct AntennaRule {
    QString gateLayer;
    QString metalLayer;
    double maxRatio;    // Max Metal Area / Gate Area
};

struct EOLRule {
    QString layerName;
    double minWidth;
    double minSpacing;  // Spacing required at the end of line
    double eolExtension; // Definition of "End of Line"
};

struct DRCViolation {
    QString ruleId;
    QString description;
    QVector<QPointF> location;  // Violation coordinates
    QString severity;           // "ERROR", "WARNING"
};

/**
 * Run DRC on current layout.
 */
QVector<DRCViolation> runDRC(const QVector<DRCRule>& rules);

/**
 * Get suggested fix for a DRC violation.
 */
QString suggestDRCFix(const DRCViolation& violation);

// ============================================================================
// FLOORPLANNING
// ============================================================================

struct FloorplanConstraint {
    QString blockName;
    QRectF allowedRegion;  // Placement region
    bool isFixed;          // Fixed vs movable
};

/**
 * Auto-generate floorplan using simulated annealing.
 */
QJsonObject generateFloorplan(const QVector<FloorplanConstraint>& constraints);

/**
 * Estimate wirelength for a given placement.
 */
double estimateWirelength(const QJsonObject& placement);

// ============================================================================
// EXPORT / IMPORT
// ============================================================================

/**
 * Export layout to GDS format.
 */
bool exportGDS(const QString& filename, const QJsonObject& options);

/**
 * Export netlist to SPICE.
 */
QString exportSPICE();

/**
 * Import from LEF/DEF (standard cell libraries).
 */
bool importLEF(const QString& filename);

/**
 * Get complete circuit state as JSON (for AI context).
 */
QJsonObject getFullCircuitState();

} // namespace AIAPI
