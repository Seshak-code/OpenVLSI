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
        case ComponentType::Probe: return "Probe";
        case ComponentType::ICPackage: return "ICPackage";
        case ComponentType::VCVS: return "VCVS";
        case ComponentType::CCVS: return "CCVS";
        case ComponentType::VCCS: return "VCCS";
        case ComponentType::CCCS: return "CCCS";
        case ComponentType::PowerPort: return "PowerPort";
        case ComponentType::RailDriver: return "RailDriver";
        case ComponentType::PowerVdd: return "PowerVdd";
        case ComponentType::PowerVss: return "PowerVss";
        case ComponentType::WaveformGen: return "WaveformGen";
        default: return "Unknown";
    }
}

ComponentType stringToComponentType(const QString& s) {
    if (s == "Resistor") return ComponentType::Resistor;
    if (s == "Capacitor" || s == "Decoupling Cap" || s == "DecouplingCap") return ComponentType::Capacitor;
    if (s == "Inductor" || s == "Ferrite Bead" || s == "FerriteBead") return ComponentType::Inductor;
    if (s == "VoltageSource" || s == "Source" || s == "Power Rail" || s == "PowerRail") return ComponentType::VoltageSource;
    if (s == "CurrentSource" || s == "ISettings") return ComponentType::CurrentSource;
    if (s == "Ground" || s == "GND" || s == "Chassis Ground" || s == "ChassisGround") return ComponentType::Ground;
    if (s == "Diode") return ComponentType::Diode;
    if (s == "Schottky" || s == "SchottkyDiode") return ComponentType::SchottkyDiode;
    if (s == "Mosfet" || s == "NMOS" || s == "PMOS") return ComponentType::Mosfet;
    if (s == "Bjt" || s == "NPN" || s == "PNP" || s == "BJT_NPN" || s == "BJT_PNP") return ComponentType::Bjt;
    if (s == "Wire") return ComponentType::Wire;
    if (s == "Probe") return ComponentType::Probe;
    if (s == "ICPackage" || s == "IC" || s == "ULN2003A" || s == "ULN2803A") return ComponentType::ICPackage;
    if (s == "VCVS") return ComponentType::VCVS;
    if (s == "CCVS") return ComponentType::CCVS;
    if (s == "VCCS") return ComponentType::VCCS;
    if (s == "CCCS") return ComponentType::CCCS;
    if (s == "PowerPort" || s == "Vin" || s == "Vout") return ComponentType::PowerPort;
    if (s == "RailDriver" || s == "Battery" || s == "LDO" || s == "Regulator") return ComponentType::RailDriver;
    if (s == "PowerVdd" || s == "Vdd" || s == "Vcc") return ComponentType::PowerVdd;
    if (s == "PowerVss" || s == "Vss" || s == "Vee") return ComponentType::PowerVss;
    if (s == "WaveformGen" || s == "SignalGen" || s == "Clock") return ComponentType::WaveformGen;
    return ComponentType::Select;
}

// ============================================================================
// PROBE TIP
// ============================================================================
ProbeTip::ProbeTip(VisualComponent* parent, bool isPos) 
    : isPositive(isPos), probeBody(parent) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setParentItem(parent);
    setZValue(3); // Above cables
}

QRectF ProbeTip::boundingRect() const {
    return QRectF(-15, -35, 30, 50);
}

QPainterPath ProbeTip::shape() const {
    QPainterPath path;
    path.addRect(-10, -10, 20, 20);
    return path;
}

void ProbeTip::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Draw the pointed probe handle/body
    painter->setPen(QPen(Qt::white, 1));
    painter->setBrush(isPositive ? QColor(255, 50, 50) : QColor(30, 30, 30));
    
    static const QPointF points[] = {
        {0, 0},     // Tip
        {-6, -20},  // Base left
        {6, -20}    // Base right
    };
    painter->drawPolygon(points, 3);
    painter->drawRect(-4, -30, 8, 10); // Handle neck
    
    // Indicator String (+/-)
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setPointSize(10);
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(QRectF(-15, -35, 30, 30), Qt::AlignCenter, isPositive ? "+" : "-");
}

QVariant ProbeTip::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionChange && scene()) {
        GridScene* gs = qobject_cast<GridScene*>(scene());
        if (gs && gs->isSnappingEnabled()) {
            QPointF newPos = value.toPointF();
            qreal x = std::round(newPos.x() / GRID_STEP) * GRID_STEP;
            qreal y = std::round(newPos.y() / GRID_STEP) * GRID_STEP;
            
            // Inform body to update its dashed lines
            if (probeBody) probeBody->update();
            
            return QPointF(x, y);
        }
    }
    return QGraphicsItem::itemChange(change, value);
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
        case ComponentType::Probe:
            name = "P?";
            value = "";
            probePositiveTip = new ProbeTip(this, true);
            probePositiveTip->setPos(0, -60);
            probeNegativeTip = new ProbeTip(this, false);
            probeNegativeTip->setPos(0, 60);
            break;
        case ComponentType::ICPackage:
            name = "U?";
            value = "ULN2003A";
            params["Model"] = "ULN2003A";
            params["PinCount"] = "16";
            paramOptions["Model"] = {"ULN2003A", "ULN2803A", "LM7805", "NE555"};
            break;
        case ComponentType::VCVS:
            name = "E?";
            value = "1.0";
            params["Role"] = "Buffer";
            break;
        case ComponentType::CCVS:
            name = "H?";
            value = "1.0";
            break;
        case ComponentType::VCCS:
            name = "G?";
            value = "1.0";
            break;
        case ComponentType::CCCS:
            name = "F?";
            value = "1.0";
            break;
        case ComponentType::PowerPort:
            name = "P?";
            value = "VDD";
            params["Net"] = "VDD";
            break;
        case ComponentType::RailDriver:
            name = "S?";
            value = "Battery";
            params["Type"] = "IdealRail";
            paramOptions["Type"] = {"IdealRail", "LinearRegulator", "SwitchingRegulator"};
            break;
        case ComponentType::PowerVdd:
            name = "VDD";
            value = "1.8V";
            break;
        case ComponentType::PowerVss:
            name = "VSS";
            value = "0V";
            break;
        case ComponentType::WaveformGen:
            name = "W?";
            value = "Clock";
            params["Frequency"] = "1MHz";
            break;
        default: 
            name = "U?"; 
            value = "0"; 
            break;
    }
}

QRectF VisualComponent::boundingRect() const {
    if (type == ComponentType::Probe) {
        return QRectF(-20, -20, 40, 40);
    }
    if (type == ComponentType::ICPackage) {
        // DIP-16 package: ~80px wide (8 pins * 10px spacing), ~120px tall
        return QRectF(-50, -70, 100, 140);
    }
    return QRectF(-50, -50, 100, 100);
}

void VisualComponent::rotate90() {
    setRotation(rotation() + 90);
}

void VisualComponent::flip() {
    // Flip horizontally (Scale X by -1)
    // Combine with existing transform
    QTransform t = transform();
    t.scale(-1, 1);
    setTransform(t);
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
        case ComponentType::Probe:
             // NO fixed terminals. Measurement points are derived from tips.
             return {};
        case ComponentType::ICPackage:
             // DIP-16 layout: 8 pins per side, 20px spacing
             // Left side pins 1-8 (bottom to top), right side pins 9-16 (top to bottom)
             for (int i = 0; i < 8; ++i) {
                 t << QPointF(-40, 60 - i * 20);  // Left side: pin 1 at bottom
             }
             for (int i = 0; i < 8; ++i) {
                 t << QPointF(40, -60 + i * 20);  // Right side: pin 16 at top
             }
             break;
        case ComponentType::VCVS:
        case ComponentType::VCCS:
            // 4 terminals: 2 control (-40, -20/20), 2 output (40, -20/20)
            t << QPointF(-40, -20) << QPointF(-40, 20) << QPointF(40, -20) << QPointF(40, 20);
            break;
        case ComponentType::CCVS:
        case ComponentType::CCCS:
            // 2 terminals (output)
            t << QPointF(40, -20) << QPointF(40, 20);
            break;
        case ComponentType::PowerPort:
            t << QPointF(0, 0); // Single connection point
            break;
        case ComponentType::RailDriver:
        case ComponentType::WaveformGen:
            t << QPointF(0, -40) << QPointF(0, 40);
            break;
        case ComponentType::PowerVdd:
            t << QPointF(0, 20); // Connect at bottom
            break;
        case ComponentType::PowerVss:
            t << QPointF(0, -20); // Connect at top
            break;
        default: break;
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
             
        case ComponentType::Probe:
            // Monochrome ASCII-Style Multimeter
            {
                // -- Virtual Wires (Dashed lines to tips) --
                if (probePositiveTip) {
                    painter->setPen(QPen(QColor(255, 100, 100), 1, Qt::DashLine));
                    painter->drawLine(QPointF(0, -10), probePositiveTip->pos());
                }
                if (probeNegativeTip) {
                    painter->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
                    painter->drawLine(QPointF(0, 10), probeNegativeTip->pos());
                }

                // -- Body (Main Chassis) --
                painter->setPen(QPen(Qt::white, 1.5));
                painter->setBrush(QColor("#1A1A1A")); // Near-black fill
                painter->drawRect(-12, -12, 24, 24);
                
                // -- Screen (LCD Display) --
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor("#2A2A2A")); // Slightly lighter grey
                painter->drawRect(-9, -9, 18, 11);
                
                // -- ASCII Value on Screen --
                painter->setPen(Qt::white);
                QFont f = painter->font();
                f.setFamily("Courier"); // Monospace for ASCII look
                f.setPointSize(7);
                f.setBold(true);
                painter->setFont(f);
                if (value.isEmpty()) {
                    painter->drawText(QRectF(-9, -9, 18, 11), Qt::AlignCenter, "---");
                } else {
                    painter->drawText(QRectF(-9, -9, 18, 11), Qt::AlignCenter, value);
                }

                // -- Dial/Button --
                painter->setBrush(QColor("#444444"));
                painter->drawRect(-4, 4, 8, 6);

                // -- Body Labels (External) --
                f.setPointSize(8);
                painter->setFont(f);
                painter->setPen(Qt::white);
                painter->drawText(QRectF(-35, -5, 20, 12), Qt::AlignRight | Qt::AlignVCenter, name);
            }
            break;
        
        case ComponentType::ICPackage:
            // DIP-16 Package Rendering (like ULN2003A)
            {
                // --- Body Rectangle ---
                painter->setPen(QPen(Qt::white, 2));
                painter->setBrush(QColor("#2D2D30"));
                painter->drawRect(-35, -60, 70, 120);
                
                // --- Notch (Orientation Marker) ---
                painter->setBrush(QColor("#1E1E1E"));
                painter->drawEllipse(-8, -64, 16, 16);
                
                // --- Model Name (Center) ---
                painter->setPen(Qt::white);
                QFont f = painter->font();
                f.setFamily("Consolas");
                f.setPointSize(9);
                f.setBold(true);
                painter->setFont(f);
                painter->drawText(QRectF(-35, -15, 70, 30), Qt::AlignCenter, value);
                
                // --- Instance Name (Above) ---
                f.setPointSize(8);
                f.setBold(false);
                painter->setFont(f);
                painter->drawText(QRectF(-35, -50, 70, 20), Qt::AlignCenter, name);
                
                // --- Pin Labels ---
                f.setPointSize(6);
                painter->setFont(f);
                
                // ULN2003A pin names (standard)
                QStringList pinNames = {"1B", "2B", "3B", "4B", "5B", "6B", "7B", "GND",
                                        "COM", "7C", "6C", "5C", "4C", "3C", "2C", "1C"};
                
                // Left side pins 1-8 (bottom to top)
                for (int i = 0; i < 8; ++i) {
                    int y = 60 - i * 20;
                    // Pin leg
                    painter->setPen(QPen(Qt::white, 1));
                    painter->drawLine(-35, y, -40, y);
                    // Pin number
                    painter->setPen(Qt::gray);
                    painter->drawText(QRectF(-33, y - 6, 15, 12), Qt::AlignLeft, QString::number(i + 1));
                    // Pin label (if available)
                    if (i < pinNames.size()) {
                        painter->setPen(QColor("#888"));
                        painter->drawText(QRectF(-55, y - 6, 15, 12), Qt::AlignRight, pinNames[i]);
                    }
                }
                
                // Right side pins 9-16 (bottom to top)
                for (int i = 0; i < 8; ++i) {
                    int y = 60 - i * 20;
                    int pinNum = 16 - i;  // Pin 16 at top
                    // Pin leg
                    painter->setPen(QPen(Qt::white, 1));
                    painter->drawLine(35, y, 40, y);
                    // Pin number
                    painter->setPen(Qt::gray);
                    painter->drawText(QRectF(20, y - 6, 15, 12), Qt::AlignRight, QString::number(pinNum));
                    // Pin label
                    int labelIdx = 15 - i;
                    if (labelIdx < pinNames.size()) {
                        painter->setPen(QColor("#888"));
                        painter->drawText(QRectF(42, y - 6, 20, 12), Qt::AlignLeft, pinNames[labelIdx]);
                    }
                }
            }
            break;
        
        case ComponentType::VCVS:
        case ComponentType::VCCS:
        case ComponentType::CCVS:
        case ComponentType::CCCS:
            // Diamond symbol for dependent sources
            {
                QPointF diamond[] = {{0, -20}, {20, 0}, {0, 20}, {-20, 0}};
                painter->drawPolygon(diamond, 4);
                // Symbol inside
                if (type == ComponentType::VCVS || type == ComponentType::CCVS) {
                    painter->drawText(QRectF(-10, -10, 20, 20), Qt::AlignCenter, "V");
                } else {
                    // Current source arrow
                    painter->drawLine(0, -10, 0, 10);
                    painter->drawLine(-3, 3, 0, 10);
                    painter->drawLine(3, 3, 0, 10);
                }
                // Control leads (dashed) for V-controlled
                if (type == ComponentType::VCVS || type == ComponentType::VCCS) {
                    painter->setPen(QPen(Qt::gray, 1, Qt::DashLine));
                    painter->drawLine(-20, -10, -40, -20);
                    painter->drawLine(-20, 10, -40, 20);
                }
                // Output leads
                painter->setPen(QPen(Qt::white, 2));
                painter->drawLine(20, -10, 40, -20);
                painter->drawLine(20, 10, 40, 20);
            }
            break;

        case ComponentType::PowerPort:
            // Pentagon / Arrow shape for Port
            {
                QPolygonF poly;
                poly << QPointF(-15, -10) << QPointF(5, -10) 
                     << QPointF(15, 0)   // Point
                     << QPointF(5, 10) << QPointF(-15, 10);
                painter->drawPolygon(poly);
                
                painter->setPen(Qt::white); // Ensure text is visible
                // Center text better
                painter->drawText(QRectF(-15, -10, 20, 20), Qt::AlignCenter, value.mid(0, 3)); 
                
                // Full label outside
                painter->setPen(Qt::gray);
                painter->drawText(QRectF(-40, -25, 80, 15), Qt::AlignCenter, value);
            }
            break;

        case ComponentType::PowerVdd:
            // Up arrow
            painter->drawLine(0, 20, 0, -10);
            painter->drawLine(0, -10, -10, 0);
            painter->drawLine(0, -10, 10, 0);
            painter->drawText(QRectF(-20, -30, 40, 20), Qt::AlignCenter, value);
            break;

        case ComponentType::PowerVss:
            // Down arrow (VSS/VEE Rail) - Distinct from Earth Ground
            painter->drawLine(0, -20, 0, 10); // Stem
            painter->drawLine(0, 10, -10, 0); // Arrow Left
            painter->drawLine(0, 10, 10, 0);  // Arrow Right
            painter->drawText(QRectF(-20, 15, 40, 20), Qt::AlignCenter, value);
            break;

        case ComponentType::RailDriver:
            // Battery icon
            painter->drawRect(-15, -25, 30, 50);
            painter->drawRect(-8, -30, 16, 5); // Cap
            painter->drawLine(0, -40, 0, -30);
            painter->drawLine(0, 25, 0, 40);
            painter->drawText(QRectF(-15, -15, 30, 30), Qt::AlignCenter, "B");
            break;

        case ComponentType::WaveformGen:
            // Sine wave icon
            painter->drawRect(-20, -20, 40, 40);
            for (int i = -15; i < 15; ++i) {
                painter->drawLine(i, 5 * sin(i * 0.3), i + 1, 5 * sin((i + 1) * 0.3));
            }
            painter->drawLine(0, -40, 0, -20);
            painter->drawLine(0, 20, 0, 40);
            break;

        default:
            painter->drawRect(-15, -15, 30, 30);
            break;
    }
    
    // VISUAL INDICATORS (X Marks)
    if (type != ComponentType::Probe) {
        painter->setPen(QPen(Qt::red, 2));
        QVector<QPointF> terms = getTerminals();
        for(const QPointF& p : terms) {
             // Draw X
             painter->drawLine(p.x()-3, p.y()-3, p.x()+3, p.y()+3);
             painter->drawLine(p.x()+3, p.y()-3, p.x()-3, p.y()+3);
        }
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
