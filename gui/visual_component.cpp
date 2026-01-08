#include "visual_component.h"
#include "schematic_editor.h" 
#include <cmath>

QString componentTypeToString(ComponentType t) {
    switch(t) {
        case ComponentType::Resistor: return "Resistor";
        case ComponentType::Capacitor: return "Capacitor";
        case ComponentType::Inductor: return "Inductor";
        case ComponentType::VoltageSource: return "VoltageSource";
        case ComponentType::Ground: return "Ground";
        case ComponentType::Diode: return "Diode";
        case ComponentType::Mosfet: return "Mosfet";
        case ComponentType::Bjt: return "Bjt";
        case ComponentType::CurrentSource: return "CurrentSource";
        case ComponentType::SchottkyDiode: return "SchottkyDiode";
        case ComponentType::Wire: return "Wire";
        default: return "Unknown";
    }
}

ComponentType stringToComponentType(const QString& s) {
    if (s == "Resistor") return ComponentType::Resistor;
    if (s == "Capacitor") return ComponentType::Capacitor;
    if (s == "Inductor") return ComponentType::Inductor;
    if (s == "VoltageSource" || s == "Source") return ComponentType::VoltageSource;
    if (s == "CurrentSource" || s == "ISettings") return ComponentType::CurrentSource;
    if (s == "Ground" || s == "GND") return ComponentType::Ground;
    if (s == "Diode") return ComponentType::Diode;
    if (s == "Schottky" || s == "SchottkyDiode") return ComponentType::SchottkyDiode;
    if (s == "Mosfet" || s == "NMOS" || s == "PMOS") return ComponentType::Mosfet;
    if (s == "Bjt" || s == "NPN" || s == "PNP" || s == "BJT_NPN" || s == "BJT_PNP") return ComponentType::Bjt;
    if (s == "Wire") return ComponentType::Wire;
    return ComponentType::Select;
}

// ============================================================================
// VISUAL WIRE
// ============================================================================
VisualWire::VisualWire(QLineF line) : QGraphicsLineItem(line) {
    setPen(QPen(Qt::lightGray, 2));
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(1); // Wires below components 
    
    // Initialize label
    labelItem = new QGraphicsTextItem(this);
    labelItem->setDefaultTextColor(Qt::yellow);
    QFont f = labelItem->font();
    f.setPointSize(10);
    labelItem->setFont(f);
    labelItem->setVisible(false); // Hidden by default if empty
}

void VisualWire::setName(const QString& name) {
    netName = name;
    if (name.isEmpty()) {
        labelItem->setVisible(false);
    } else {
        labelItem->setPlainText(name);
        labelItem->setVisible(true);
        // Center label
        QPointF mid = line().pointAt(0.5);
        labelItem->setPos(mid.x(), mid.y() - 15); // Slightly above
    }
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
            paramOptions["Tolerance"] = {"1%", "5%", "10%", "20%"};
            break;
        case ComponentType::Capacitor: 
            name = "C?"; 
            value = "1uF"; 
            params["Voltage Rating"] = "50V"; 
            paramOptions["Voltage Rating"] = {"6.3V", "10V", "16V", "25V", "50V", "100V"};
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
            paramOptions["Model"] = {"nfet_01v8", "pfet_01v8", "nfet_03v3", "pfet_03v3"};
            break;
        case ComponentType::Bjt:
            name = "Q?";
            value = "NPN";
            params["Model"] = "2N2222";
            paramOptions["Model"] = {"2N2222", "2N3904", "2N3906", "BC547"};
            break;
        case ComponentType::CurrentSource:
            name = "I?";
            value = "1mA";
            break;
        case ComponentType::SchottkyDiode:
            name = "D?";
            value = "BAT54";
            break;
        default: 
            name = "U?"; 
            value = "0"; 
            break;
    }
}

QRectF VisualComponent::boundingRect() const {
    // Inductors and some mosfets might need slightly wider boxes
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
        case ComponentType::SchottkyDiode:
            t << QPointF(-20, 0) << QPointF(20, 0);
            break;
        case ComponentType::VoltageSource:
        case ComponentType::CurrentSource:
            t << QPointF(0, -40) << QPointF(0, 40);
            break;
        case ComponentType::Bjt: 
            // Base (-20, 0), Collector (20, -20), Emitter (20, 20)
            t << QPointF(-20, 0) << QPointF(20, -20) << QPointF(20, 20);
            break;
        case ComponentType::Ground:
            t << QPointF(0, -20); 
            break;
        case ComponentType::Mosfet:
             // Gate (-20, 0), Drain (20, -20), Source (20, 20)
             t << QPointF(-20, 0) << QPointF(20, -20) << QPointF(20, 20); 
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
        
        case ComponentType::Inductor:
            // Draw Coils
            {
                painter->drawLine(-40, 0, -30, 0); // Left lead
                painter->drawLine(30, 0, 40, 0);   // Right lead
                
                // 3 Loops
                painter->drawArc(-30, -10, 20, 20, 0, 180 * 16);
                painter->drawArc(-10, -10, 20, 20, 0, 180 * 16);
                painter->drawArc(10, -10, 20, 20, 0, 180 * 16);
            }
            break;
            
        case ComponentType::CurrentSource:
            painter->drawEllipse(-20, -20, 40, 40);
            painter->drawLine(0, -15, 0, 15);
            // Arrow Head
            painter->drawLine(-5, -5, 0, -15);
            painter->drawLine(5, -5, 0, -15);
            
            painter->drawLine(0, -40, 0, -20);
            painter->drawLine(0, 20, 0, 40);
            break;
            
        case ComponentType::Diode:
        case ComponentType::SchottkyDiode:
            painter->drawLine(-5, -10, -5, 10); // Anode
            {
                // Triangle
                QPointF poly[] = {{10, 0}, {-5, -10}, {-5, 10}};
                painter->drawPolygon(poly, 3);
            }
            // Cathode Bar
            if (type == ComponentType::SchottkyDiode) {
                // S shape
                painter->drawLine(10, -10, 10, 10);
                painter->drawLine(10, -10, 14, -10); // Top hook right
                painter->drawLine(10, 10, 6, 10);    // Bottom hook left
            } else {
                painter->drawLine(10, -10, 10, 10);
            }
            
            painter->drawLine(-20, 0, -5, 0);
            painter->drawLine(10, 0, 20, 0);
            break;
            
        case ComponentType::Mosfet:
            // Draw NMOS/PMOS Symbol
            {
                bool isPMOS = (value.contains("PMOS") || value.contains("pfet"));
                
                // Leads
                painter->drawLine(-20, 0, -10, 0); // Gate lead
                painter->drawLine(0, 20, 10, 20);  // Drain/Source lead
                painter->drawLine(20, 0, 10, 0);   // Drain/Source lead
                
                // Gate Plate
                painter->drawLine(-10, -15, -10, 15);
                
                // Channel Plates (3 dashed segments)
                painter->drawLine(0, -15, 0, -9);
                painter->drawLine(0, -3, 0, 3);
                painter->drawLine(0, 9, 0, 15);
                
                // Substrate
                painter->drawLine(0, 0, 10, 0); // Bulk connection
                // Arrow
                if (isPMOS) {
                    // Arrow pointing OUT from Channel (on Bulk)
                    painter->drawLine(5, 0, 2, -3);
                    painter->drawLine(5, 0, 2, 3);
                    // Gate bubble
                     painter->drawEllipse(-12, -2, 4, 4);
                } else {
                    // NMOS: Arrow pointing IN to Channel
                    painter->drawLine(0, 0, 5, -3);
                    painter->drawLine(0, 0, 5, 3);
                }
                
                // Connect D/S to channel
                painter->drawLine(10, -15, 0, -15);
                painter->drawLine(10, 15, 0, 15);
                
                // Vertical D/S lines to new terminals
                // Drain (20, -20) -> Dogleg
                painter->drawLine(10, -15, 20, -15);
                painter->drawLine(20, -15, 20, -20);
                
                // Source (20, 20) -> Dogleg
                painter->drawLine(10, 15, 20, 15);
                painter->drawLine(20, 15, 20, 20);
            }
            break;
            
        case ComponentType::Bjt:
             // Draw BJT Symbol
             {
                 bool isPNP = (value.contains("PNP"));
                 
                 // Base
                 painter->drawLine(-10, -10, -10, 10); // Base Bar
                 painter->drawLine(-20, 0, -10, 0);    // Base Lead
                 
                 // Collector (Angled)
                 painter->drawLine(-10, -5, 10, -15);
                 // Lead to (20, -20)
                 painter->drawLine(10, -15, 20, -15);
                 painter->drawLine(20, -15, 20, -20);
                 
                 // Emitter (Angled)
                 painter->drawLine(-10, 5, 10, 15);
                 // Lead to (20, 20)
                 painter->drawLine(10, 15, 20, 15);
                 painter->drawLine(20, 15, 20, 20);
                 
                 // Arrow
                 if (isPNP) {
                     // Emitter Arrow Pointing IN
                     QPointF mid(0, 10); // on emitter leg
                     painter->drawLine(mid.x(), mid.y(), mid.x()-3, mid.y()-5);
                     painter->drawLine(mid.x(), mid.y(), mid.x()+5, mid.y()-2);
                 } else {
                     // NPN: Emitter Arrow Pointing OUT
                     QPointF tip(10, 15);
                     painter->drawLine(tip.x(), tip.y(), tip.x()-6, tip.y()-2);
                     painter->drawLine(tip.x(), tip.y(), tip.x()-2, tip.y()-6);
                 }
             }
             break;
             
        default:
            painter->drawRect(-15, -15, 30, 30);
            break;
    }
    
    // VISUAL INDICATORS (X Marks)
    painter->setPen(QPen(Qt::red, 2));
    QVector<QPointF> terms = getTerminals();
    for(const QPointF& p : terms) {
         // Draw X
         painter->drawLine(p.x()-3, p.y()-3, p.x()+3, p.y()+3);
         painter->drawLine(p.x()+3, p.y()-3, p.x()-3, p.y()+3);
    }

    // ====== PLACEMENT PRE-FLIGHT INDICATOR ======
    // Draw colored border based on placement validity (set by GridScene::mouseMoveEvent)
    QString placementState = data(0).toString();
    if (!placementState.isEmpty()) {
        if (placementState == "valid") {
            painter->setPen(QPen(QColor(0, 255, 0), 2, Qt::DashLine)); // Green
        } else {
            painter->setPen(QPen(QColor(255, 0, 0), 2, Qt::DashLine)); // Red
        }
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
    }

    if (isSelected()) {
        painter->setPen(QPen(QColor(0, 255, 0), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
    }
}
