#pragma once

#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QPen>
#include <QPainter>
#include <QMap>
#include <QVector>

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
    JFET_N,         // N-Channel JFET
    JFET_P,         // P-Channel JFET
    CurrentSource,
    SchottkyDiode,
    Probe,
    ICPackage,      // Hierarchical IC (e.g., ULN2003A, 555 Timer)
    
    // Dependent Sources (Bench + Block internal)
    VCVS,           // Voltage-Controlled Voltage Source
    CCVS,           // Current-Controlled Voltage Source
    VCCS,           // Voltage-Controlled Current Source
    CCCS,           // Current-Controlled Current Source
    
    // Block mode (PowerPort never stamps MNA - constraint endpoint only)
    PowerPort,      // Interface pin: Vin, Vout, GND as requirement
    
    // System mode (RailDrivers inject net power)
    RailDriver,     // Abstract: IdealRail, LinearRegulator, SwitchingRegulator
    PowerVdd,       // Vdd/Vcc symbol (binds to global net)
    PowerVss,       // Vss/GND symbol (binds to global net)
    WaveformGen     // System-level clock/IO driver
};

QString componentTypeToString(ComponentType t);
ComponentType stringToComponentType(const QString& s);

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

    // Helper to get effective pen
    QPen pen() const { return m_pen; }
    void setPen(const QPen& p) { m_pen = p; update(); }

private:
    QLineF m_line;
    QPen m_pen; // QGraphicsObject doesn't store a pen like QGraphicsLineItem/PathItem does
};

// ============================================================================
// PROBE TIP
// ============================================================================
class VisualComponent;
class ProbeTip : public QGraphicsItem {
public:
    ProbeTip(VisualComponent* parent, bool isPositive);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    // Snapping support
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

    bool isPositive;
    VisualComponent* probeBody;
};

// ============================================================================
// VISUAL COMPONENT
// ============================================================================
class VisualComponent : public QGraphicsObject {
    Q_OBJECT
public:
    ComponentType type;
    QString name;
    QString value;
    QMap<QString, QString> params; // Dynamic parameters
    QMap<QString, QStringList> paramOptions; // Options for parameters (Key -> List of Choices)

    // For Oscilloscope-style Probes
    ProbeTip* probePositiveTip = nullptr;
    ProbeTip* probeNegativeTip = nullptr;

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

protected:
    // Hover events for quick preview tooltip
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    
    // Tab/Esc handling for Ghost acceptance
    void keyPressEvent(QKeyEvent* event) override;
};
