#include "visual_component.h"
#include "schematic_editor.h" 
#include <cmath>

// ============================================================================
// VISUAL WIRE
// ============================================================================
VisualWire::VisualWire(QLineF line) : QGraphicsLineItem(line) {
    setPen(QPen(Qt::lightGray, 2));
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(1); // Wires below components 
}

void VisualWire::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    painter->setPen(pen());
    if (isSelected()) {
        painter->setPen(QPen(Qt::green, 2, Qt::DashLine));
    }
    painter->drawLine(line());
}

QVariant VisualWire::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionChange && scene()) {
        GridScene* gs = qobject_cast<GridScene*>(scene());
        if (gs && gs->isSnappingEnabled()) {
            QPointF newPos = value.toPointF();
            qreal x = std::round(newPos.x() / GRID_STEP) * GRID_STEP;
            qreal y = std::round(newPos.y() / GRID_STEP) * GRID_STEP;
            return QPointF(x, y);
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

// ============================================================================
// VISUAL COMPONENT
// ============================================================================
VisualComponent::VisualComponent(ComponentType t) : type(t) {
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setTransformOriginPoint(0, 0); 
    setZValue(2); // Components above wires
    
    // Default Properties
    switch(t) {
        case ComponentType::Resistor: 
            name = "R?"; 
            value = "1k"; 
            params["Tolerance"] = "5%";
            break;
        case ComponentType::Capacitor: 
            name = "C?"; 
            value = "1uF"; 
            params["Voltage Rating"] = "50V"; 
            break;
        case ComponentType::Inductor: 
            name = "L?"; 
            value = "1mH"; 
            break;
        case ComponentType::VoltageSource: 
            name = "V?"; 
            value = "5V"; 
            params["AC Mag"] = "0"; 
            params["AC Phase"] = "0";
            break;
        case ComponentType::Mosfet:
            name = "M?";
            value = "NMOS";
            params["W"] = "1u";
            params["L"] = "180n";
            params["Model"] = "nfet_01v8";
            break;
        case ComponentType::Bjt:
            name = "Q?";
            value = "NPN";
            params["Model"] = "2N2222";
            break;
        default: 
            name = "U?"; 
            value = "0"; 
            break;
    }
}

QRectF VisualComponent::boundingRect() const {
    return QRectF(-50, -50, 100, 100);
}

void VisualComponent::rotate90() {
    setRotation(rotation() + 90);
}

QVector<QPointF> VisualComponent::getTerminals() const {
    // Terminals MUST land on 20px grid (GRID_STEP)
    QVector<QPointF> t;
    switch(type) {
        case ComponentType::Resistor:
        case ComponentType::Inductor:
            t << QPointF(-40, 0) << QPointF(40, 0);
            break;
        case ComponentType::Capacitor:
        case ComponentType::Diode:
            t << QPointF(-20, 0) << QPointF(20, 0);
            break;
        case ComponentType::VoltageSource:
        case ComponentType::Bjt: 
            t << QPointF(0, -40) << QPointF(0, 40);
            break;
        case ComponentType::Ground:
            t << QPointF(0, -20); 
            break;
        case ComponentType::Mosfet:
             t << QPointF(-20, 0) << QPointF(20, 0) << QPointF(0, 20); // G D S
             break;
        default:
            break;
    }
    return t;
}

QPainterPath VisualComponent::shape() const {
    QPainterPath path;
    // Define the "Solid Body" that wires cannot cross
    switch(type) {
        case ComponentType::Resistor:
        case ComponentType::Inductor:
            path.addRect(-20, -10, 40, 20);
            break;
        case ComponentType::Capacitor:
        case ComponentType::Diode:
            path.addRect(-10, -15, 20, 30);
            break;
        case ComponentType::VoltageSource:
            path.addEllipse(-20, -20, 40, 40);
            break;
        case ComponentType::Ground:
            path.addRect(-10, -5, 20, 10); 
            break;
        default:
            path.addRect(-15, -15, 30, 30);
            break;
    }
    return path;
}

QVariant VisualComponent::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionChange && scene()) {
        GridScene* gs = qobject_cast<GridScene*>(scene());
        if (gs && gs->isSnappingEnabled()) {
            QPointF newPos = value.toPointF();
            qreal x = std::round(newPos.x() / GRID_STEP) * GRID_STEP;
            qreal y = std::round(newPos.y() / GRID_STEP) * GRID_STEP;
            return QPointF(x, y);
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

void VisualComponent::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setPen(QPen(Qt::white, 2));
    painter->setBrush(QColor("#1e1e1e")); 
    
    // Draw Components (Grid Aligned)
    switch(type) {
        case ComponentType::Resistor:
            painter->drawRect(-20, -6, 40, 12);
            painter->drawLine(-40, 0, -20, 0); 
            painter->drawLine(20, 0, 40, 0);
            break;
        case ComponentType::Capacitor:
            painter->drawLine(-5, -15, -5, 15);
            painter->drawLine(5, -15, 5, 15);
            painter->drawLine(-20, 0, -5, 0);
            painter->drawLine(5, 0, 20, 0);
            break;
        case ComponentType::Ground:
            painter->drawLine(-15, 0, 15, 0);
            painter->drawLine(-8, 5, 8, 5);
            painter->drawLine(-2, 10, 2, 10);
            painter->drawLine(0, -20, 0, 0); 
            break;
        case ComponentType::VoltageSource:
            painter->drawEllipse(-20, -20, 40, 40);
            painter->drawText(QRectF(-15, -15, 30, 30), Qt::AlignCenter, "V");
            painter->drawLine(0, -40, 0, -20);
            painter->drawLine(0, 20, 0, 40);
            break;
        case ComponentType::Diode:
            painter->drawLine(-5, -10, -5, 10); 
            {
                QPointF poly[] = {{10, 0}, {-5, -10}, {-5, 10}};
                painter->drawPolygon(poly, 3);
            }
            painter->drawLine(-20, 0, -5, 0);
            painter->drawLine(10, 0, 20, 0);
            break;
        default:
            painter->drawRect(-15, -15, 30, 30);
            break;
    }
    
    // VISUAL INDICATORS
    painter->setPen(QPen(Qt::red, 1));
    QVector<QPointF> terms = getTerminals();
    for(const QPointF& p : terms) {
         painter->drawLine(p.x()-4, p.y()-4, p.x()+4, p.y()+4);
         painter->drawLine(p.x()-4, p.y()+4, p.x()+4, p.y()-4);
    }

    if (isSelected()) {
        painter->setPen(QPen(QColor(0, 255, 0), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
    }
}
