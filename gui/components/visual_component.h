#pragma once

#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QPen>
#include <QPainter>
#include <QMap>
#include <QVector>
#include "../../components/design_db.h"

const int GRID_STEP = 20;

enum class ComponentType {
    Select,
    Wire,
    Resistor,
    Capacitor,
    Inductor,
    VoltageSource,
    Ground,
    Diode,
    Mosfet,
    Bjt,
    CurrentSource,
    Probe,
    SchottkyDiode,

    // PDK-Ready Primitives (Full Simulation Support)
    PDK_NFET,       // 4-Terminal (D, G, S, B)
    PDK_PFET,       // 4-Terminal (D, G, S, B)
    PDK_Resistor,   // 3-Terminal (A, B, SUB)
    PDK_Capacitor,  // 2-Terminal (TOP, BOT)

    // Power Rails (Domain-aware)
    PDK_VDD,        // Positive Core Supply (e.g., 1.8V, 3.3V)
    PDK_VSS,        // Ground Supply (0V reference)
    PDK_VDDIO,      // I/O Supply Domain (typically higher voltage)
    PDK_VBB,        // Body Bias Supply (for back-gate control)

    // Dependent Sources
    VCVS,           // Voltage-Controlled Voltage Source
    CCVS,           // Current-Controlled Voltage Source
    VCCS,           // Voltage-Controlled Current Source
    CCCS,           // Current-Controlled Current Source
    
    SubCircuit,      // Generic SubCircuit (Standard Cell)
    ICPackage,      // Hierarchical (File-Based)
    Pin,             // External Interface Pin
    Generic         // User-Defined Physics Model (Verilog-A)
};


QString componentTypeToString(ComponentType t);
ComponentType stringToComponentType(const QString& s);

// ============================================================================
// HIERARCHICAL PIN METADATA
// ============================================================================
// Used by ICPackage components to store pin information from child schematics.
// Supports any PDK-based design domain: Digital, Analog, RF, Mixed-Signal.
struct PinInfo {
    QString name;                      // Pin name (e.g., "CLK", "VIN", "RF_OUT")
    enum class Direction { Input, Output, BiDir, Power } direction = Direction::Input;
    QString busGroup;                  // Bus group name (e.g., "ADDR", "DATA") 
    int busIndex = -1;                 // Index within bus (-1 if not a bus pin)
    bool isPower = false;              // VDD, VSS, VCC, GND, etc.
    QString domain;                    // Optional: "digital", "analog", "rf", "power"
    
    // For analog/RF: impedance or other metadata
    QString impedance;                 // e.g., "50Ω" for RF
    QString netType;                   // e.g., "differential", "single-ended"
};

// Phase 11.6: Ghosting System
enum class GhostState {
    None,       // Normal component
    Preview,    // AI speculative (50% opacity, dashed)
    Focused,    // Hovered/Selected (Highlight + Tooltip)
    Accepted,   // Transition to Normal (Fade to solid)
    Rejected    // Fade out
};

// ============================================================================
// VISUAL WIRE
// ============================================================================
// Note: We inherit QGraphicsObject (for QPointer) but we must manually handle the line shape
// since we can't multiple-inherit QGraphicsLineItem + QGraphicsObject easily.
// Ideally usage of QPointer requires QObject. 
// VisualWire was QGraphicsLineItem. We will change to QGraphicsObject and implement shape()/boundingRect() manually like a line.

class VisualWire : public QGraphicsObject {
    Q_OBJECT
public:
    VisualWire(QLineF line);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

    // Line management (since we are no longer QGraphicsLineItem)
    void setLine(const QLineF& line);
    QLineF line() const { return m_line; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;
    
    // Snapping support
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

    // Net Naming
    void setName(const QString& name);
    QString getName() const { return netName; }

    QString netName;
    QGraphicsTextItem* labelItem = nullptr;
    
    // Physics Properties
    double lineWidth = 2.0; 
    double maxCurrent = 0.0; // Amps
    
    void setWidth(double width) {
        lineWidth = width;
        m_pen.setWidthF(width);
        update(); // Trigger repaint
    }
    
    // Phase 1.5: Parasitic/Physical Properties
    QString layer = "M1"; // Default Metal 1
    double width_um = 0.5; // Default 0.5um
    QString model_name = "wire_generic"; // Generic model for R/C lookup

    // Helper to get effective pen
    QPen pen() const { return m_pen; }
    void setPen(const QPen& p) { m_pen = p; update(); }

private:
    QLineF m_line;
    QPen m_pen; // QGraphicsObject doesn't store a pen like QGraphicsLineItem/PathItem does
};

// ProbeTip included from probe_tip.h
#include "probe_tip.h"

// ============================================================================
// VISUAL COMPONENT
// ============================================================================
class VisualComponent : public QGraphicsObject {
    Q_OBJECT
    Q_PROPERTY(QPointF pos READ pos WRITE setPos)

public:
    ComponentType type;
    QString name;
    QString value; // e.g., "10k"
    QString subCircuitName; // For Standard Cells / Library Items
    QString sourceFilePath; // For File-Based Hierarchical Blocks
    
    // Pin Properties
    enum class PinDirection { Input, Output, BiDir };
    PinDirection pinDirection = PinDirection::Input;
    QMap<QString, QString> params; // Dynamic parameters
    QMap<QString, QStringList> paramOptions; // Options for parameters (Key -> List of Choices)
    
    // Paradigm Bridge: Link to Unified DesignDb
    acutesim::components::InstUUID dbId;

    // Probe Support (Late Binding)
    struct ProbeBinding {
        QStringList hierarchicalPath; // e.g. ["X1", "X2"]
        QString localNetName;         // e.g. "VOUT"
    };
    ProbeBinding probeBinding;

    // For Oscilloscope-style Probes
    ProbeTip* probePositiveTip = nullptr;
    ProbeTip* probeNegativeTip = nullptr;
    
    // For ICPackage: Dynamic pins loaded from child schematic
    QVector<PinInfo> pinList;
    void loadPinsFromSchematic(const QString& schPath);
    QString displayName;  // User-friendly name for rendering

    VisualComponent(ComponentType t);

    QRectF boundingRect() const override;
    QPainterPath shape() const override; 
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;
    
    QVector<QPointF> getTerminals() const; 

    // Snapping support
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    
    void rotate90(); 
    void flip(); // Flips horizontally around center 
    
    // Quick Preview (Phase 1.7)
    // Stores estimated voltage for display (set by GridScene)
    double quickPreviewVoltage = 0.0;
    bool hasQuickPreview = false;
    
    // Set quick preview data (called by scene after QuickAnalysis)
    void setQuickPreview(double voltage) {
        quickPreviewVoltage = voltage;
        hasQuickPreview = true;
        update(); // Trigger repaint to show tooltip
    }
    
    void clearQuickPreview() {
        hasQuickPreview = false;
        update();
    }

    // Phase 11.6: Ghosting Support
    GhostState ghostState = GhostState::None;
    QString ghostWhy;     // Rationale for "Why?" tooltip
    float ghostConfidence = 1.0f; // 0.0 to 1.0

    bool isGhost() const { return ghostState != GhostState::None; }
    void setGhostState(GhostState s) {
        ghostState = s;
        update();
    }
    
    // Explicit exclusions for simulation
    bool isNetlistMember() const {
        return !isGhost();
    }

    void updateCachedPens();

protected:
    // Optimization: Cached Theme Objects
    QPen m_bodyPen;
    QBrush m_bodyBrush;
    QPen m_ghostPen;
    QBrush m_ghostBrush;
    QPen m_selectionPen; // Cached selection dash

    // Hover events for quick preview tooltip
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    
    // Tab/Esc handling for Ghost acceptance
    void keyPressEvent(QKeyEvent* event) override;
};
