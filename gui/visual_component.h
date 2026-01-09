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
    CurrentSource,
    SchottkyDiode,
    Probe
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
};
