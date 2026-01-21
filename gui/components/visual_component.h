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

// ============================================================================
// VISUAL WIRE
// ============================================================================
class VisualWire : public QGraphicsLineItem {
public:
    VisualWire(QLineF line);

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
        QPen p = pen();
        p.setWidthF(width);
        setPen(p);
    }
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
class VisualComponent : public QGraphicsItem {
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

protected:
    // Hover events for quick preview tooltip
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
};
