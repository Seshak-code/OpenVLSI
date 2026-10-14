#include "visual_component.h"
#include "../editors/schematic/schematic_editor.h" 
#include "../../components/standard_cells.h"
#include <cmath>
#include <QGraphicsSceneHoverEvent>
#include <QKeyEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include "../../gui/styling/theme_manager.h"

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
        case ComponentType::PDK_NFET: return "PDK_NFET";
        case ComponentType::PDK_PFET: return "PDK_PFET";
        case ComponentType::PDK_Resistor: return "PDK_Resistor";
        case ComponentType::PDK_Capacitor: return "PDK_Capacitor";
        case ComponentType::VCVS: return "VCVS";
        case ComponentType::CCVS: return "CCVS";
        case ComponentType::VCCS: return "VCCS";
        case ComponentType::CCCS: return "CCCS";
        case ComponentType::PDK_VDD: return "PDK_VDD";
        case ComponentType::PDK_VSS: return "PDK_VSS";
        case ComponentType::PDK_VDDIO: return "PDK_VDDIO";
        case ComponentType::PDK_VBB: return "PDK_VBB";
        case ComponentType::SubCircuit: return "SubCircuit";
        case ComponentType::ICPackage: return "ICPackage";
        case ComponentType::Pin: return "Pin";
        case ComponentType::PowerPort: return "PowerPort";
        case ComponentType::Generic: return "Generic";
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
    if (s == "PowerPort" || s == "Power Vdd" || s == "Power Vss") return ComponentType::PowerPort;
    if (s == "RailDriver" || s == "WaveformGen") return ComponentType::VoltageSource;
    if (s == "PDK_NFET") return ComponentType::PDK_NFET;
    if (s == "PDK_PFET") return ComponentType::PDK_PFET;
    if (s == "PDK_Resistor") return ComponentType::PDK_Resistor;
    if (s == "PDK_Capacitor") return ComponentType::PDK_Capacitor;
    if (s == "VCVS") return ComponentType::VCVS;
    if (s == "CCVS") return ComponentType::CCVS;
    if (s == "VCCS") return ComponentType::VCCS;
    if (s == "CCCS") return ComponentType::CCCS;
    if (s == "CCCS") return ComponentType::CCCS;
    if (s == "PDK_VDD" || s == "VDD") return ComponentType::PDK_VDD;
    if (s == "PDK_VSS" || s == "VSS") return ComponentType::PDK_VSS;
    if (s == "PDK_VDDIO" || s == "VDDIO") return ComponentType::PDK_VDDIO;
    if (s == "PDK_VBB" || s == "VBB") return ComponentType::PDK_VBB;
    if (s == "SubCircuit") return ComponentType::SubCircuit;
    if (s == "ICPackage") return ComponentType::ICPackage;
    if (s == "Pin") return ComponentType::Pin;
    if (s == "Generic") return ComponentType::Generic;
    return ComponentType::Select;
}

// ============================================================================
// PROBE TIP
// ============================================================================
// Old ProbeTip inline implementation removed.

// Old ProbeTip::itemChange removed.

// ============================================================================
// VISUAL WIRE
// ============================================================================
// ============================================================================
// VISUAL WIRE
// ============================================================================
VisualWire::VisualWire(QLineF line) : QGraphicsObject() {
    m_line = line;
    setPen(QPen(acutesim::gui::styling::ThemeManager::instance().getColor("schematic.wire"), 2));
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(1); // Wires below components 
    
    // Initialize label
    labelItem = new QGraphicsTextItem(this);
    labelItem->setDefaultTextColor(acutesim::gui::styling::ThemeManager::instance().getColor("ui.text"));
    QFont f = labelItem->font();
    f.setPointSize(10);
    labelItem->setFont(f);
    labelItem->setVisible(false); // Hidden by default if empty
}

void VisualWire::setLine(const QLineF& line) {
    if (m_line == line) return;
    prepareGeometryChange();
    m_line = line;
    update();
}

QRectF VisualWire::boundingRect() const {
    qreal extra = (m_pen.widthF() + 10) / 2.0;
    return QRectF(m_line.p1(), m_line.p2())
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

QPainterPath VisualWire::shape() const {
    QPainterPath path;
    if (m_line.length() == 0) return path;
    
    QPainterPathStroker stroker;
    stroker.setWidth(m_pen.widthF() + 10); // Generous hit area
    stroker.setCapStyle(Qt::RoundCap);
    path.moveTo(m_line.p1());
    path.lineTo(m_line.p2());
    return stroker.createStroke(path);
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
        painter->setPen(QPen(acutesim::gui::styling::ThemeManager::instance().getColor("schematic.selection"), 2, Qt::DashLine));
    }
    painter->drawLine(m_line);
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
void VisualComponent::updateCachedPens() {
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    
    QColor outline = tm.getColor("schematic.component_outline");
    QColor fill = tm.getColor("schematic.component_fill");
    
    m_bodyPen = QPen(outline, 2);
    m_bodyPen.setJoinStyle(Qt::RoundJoin);
    
    m_bodyBrush = QBrush(fill);
    
    // Ghost
    QColor ghostColor = tm.getColor("schematic.ghost");
    if (!ghostColor.isValid()) ghostColor = QColor(0, 200, 255);
    
    QColor ghostFill = ghostColor;
    ghostFill.setAlpha(32);
    m_ghostBrush = QBrush(ghostFill);
    
    m_ghostPen = QPen(ghostColor, 2, Qt::DashLine);
    
    // Selection
    QColor selColor = tm.getColor("schematic.selection");
    m_selectionPen = QPen(selColor, 2, Qt::DashLine);
}

// Independent V/I source fields, named and grouped as in Cadence analogLib
// vsource/isource. `q` is "Voltage"/"Current"; `u` scales level defaults (1 V, 1 mA).
static void initSourceParams(QMap<QString, QString>& p, QMap<QString, QStringList>& opts,
                             const QString& q, const QString& u) {
    p["Source type"] = "dc";
    opts["Source type"] = {"dc", "sine", "pulse"};
    p["AC magnitude"] = "0";
    p["AC phase"] = "0";
    p[q + " 1"] = "0";
    p[q + " 2"] = "1" + u;
    p["Delay time"] = "0";
    p["Rise time"] = "1n";
    p["Fall time"] = "1n";
    p["Pulse width"] = "5n";
    p["Period"] = "10n";
    p["Offset " + q.toLower()] = "0";
    p["Amplitude"] = "1" + u;
    p["Frequency"] = "1k";
    p["Damping factor"] = "0";
    p["Initial phase"] = "0";
}

QStringList VisualComponent::propertyKeys() const {
    const bool isV = type == ComponentType::VoltageSource;
    if (!isV && type != ComponentType::CurrentSource) return params.keys();
    const QString q = isV ? "Voltage" : "Current";
    QStringList keys{"Source type", "AC magnitude", "AC phase"};
    const QString t = params.value("Source type");
    if (t == "pulse")
        keys << q + " 1" << q + " 2" << "Delay time" << "Rise time" << "Fall time" << "Pulse width" << "Period";
    else if (t == "sine")
        keys << "Offset " + q.toLower() << "Amplitude" << "Frequency" << "Delay time" << "Damping factor" << "Initial phase";
    return keys;
}

QString VisualComponent::paramUnit(const QString& key) const {
    if (type != ComponentType::VoltageSource && type != ComponentType::CurrentSource) return "";
    if (key == "Source type") return "";
    if (key.endsWith(" time") || key == "Pulse width" || key == "Period") return "s";
    if (key == "Frequency") return "Hz";
    if (key.endsWith(" phase")) return "deg";
    if (key == "Damping factor") return "1/s";
    return type == ComponentType::VoltageSource ? "V" : "A";
}

double parseSiValue(const QString& str, bool* ok) {
    if (ok) *ok = true;
    const QString s = str.trimmed();
    if (s.isEmpty()) return 0.0;

    // Split into numeric part and suffix
    QString numStr, suffix;
    bool suffixFound = false;
    for (QChar c : s) {
        if (!suffixFound && (c.isDigit() || c == '.' || c == '-' || c == '+' || c.toLower() == 'e')) {
            numStr.append(c);
        } else {
            suffixFound = true;
            suffix.append(c);
        }
    }
    bool numOk = false;
    double val = numStr.toDouble(&numOk);
    if (ok) *ok = numOk;
    suffix = suffix.trimmed();

    if (suffix.startsWith("meg", Qt::CaseInsensitive) || suffix.startsWith('M')) val *= 1e6;
    else if (suffix.startsWith('T')) val *= 1e12;
    else if (suffix.startsWith('G')) val *= 1e9;
    else if (suffix.startsWith('k') || suffix.startsWith('K')) val *= 1e3;
    else if (suffix.startsWith('m')) val *= 1e-3;
    else if (suffix.startsWith('u')) val *= 1e-6;
    else if (suffix.startsWith('n')) val *= 1e-9;
    else if (suffix.startsWith('p')) val *= 1e-12;
    else if (suffix.startsWith('f')) val *= 1e-15;
    return val;
}

VisualComponent::VisualComponent(ComponentType t) : type(t) {
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setTransformOriginPoint(0, 0); 
    setZValue(2); // Components above wires
    setAcceptHoverEvents(true); // Enable hover for Quick Preview
    
    updateCachedPens(); // Initial cache population

    
    // Default Properties
    switch(t) {
        case ComponentType::Resistor: 
            name = "R?"; 
            value = "1k"; 
            params["Tolerance"] = "5%";
            paramOptions["Tolerance"] = {"1%", "5%", "10%", "20%"};
            // PDK Transformation Support
            params["Model"] = ""; 
            params["W"] = "";
            params["L"] = "";
            break;
        case ComponentType::Capacitor: 
            name = "C?"; 
            value = "1uF"; 
            params["Voltage Rating"] = "50V"; 
            paramOptions["Voltage Rating"] = {"6.3V", "10V", "16V", "25V", "50V", "100V"};
            // PDK Transformation Support
            params["Model"] = "";
            params["W"] = "";
            params["L"] = "";
            break;
        case ComponentType::Inductor: 
            name = "L?"; 
            value = "1mH"; 
            // PDK Transformation Support
            params["Model"] = "";
            break;
        case ComponentType::VoltageSource: 
            name = "V?"; 
            value = "5V"; 
            initSourceParams(params, paramOptions, "Voltage", "");
            break;
        case ComponentType::Mosfet:
            name = "M?";
            value = "NMOS";
            params["W"] = "1u";
            params["L"] = "0.18u";
            params["Model"] = "nfet_01v8";
            paramOptions["Model"] = {"nfet_01v8", "pfet_01v8"};
            break;
        case ComponentType::Bjt:
            name = "Q?";
            value = "NPN";
            params["Model"] = "NPN_GEN";
            paramOptions["Model"] = {"NPN_GEN", "PNP_GEN"};
            break;
        case ComponentType::Probe:
            name = "Probe?";
            // Initialize child tips
            probePositiveTip = new ProbeTip(ProbeTip::Positive, this);
            probeNegativeTip = new ProbeTip(ProbeTip::Negative, this);
            probePositiveTip->setPos(40, -40); // Initial offset
            probeNegativeTip->setPos(40, 40);
            break;
        case ComponentType::PDK_NFET:
            name = "MN?";
            value = "PDK_NFET";
            params["Model"] = "nfet_01v8"; // Default PDK model
            params["W"] = "1.0u";
            params["L"] = "0.18u";
            break;
        case ComponentType::PDK_PFET:
            name = "MP?";
            value = "PDK_PFET";
            params["Model"] = "pfet_01v8"; // Default PDK model
            params["W"] = "1.0u";
            params["L"] = "0.18u";
            break;
        case ComponentType::PDK_Resistor:
            name = "RP?";
            value = "PDK_PolyRes";
            params["R"] = "1k";
            break;
        case ComponentType::PDK_Capacitor:
            name = "CP?";
            value = "PDK_MIMCap";
            params["C"] = "1p";
            break;
        case ComponentType::CurrentSource:
            name = "I?";
            value = "1mA";
            initSourceParams(params, paramOptions, "Current", "m");
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
        case ComponentType::SubCircuit:
            name = "X?";
            value = "SubCkt";
            break;
        case ComponentType::ICPackage:
            name = "IC?";
            value = "Package";
            // File-based hierarchical component
            // sourceFilePath will be set when loading from JSON
            break;
        case ComponentType::Pin:
            name = "PIN?";
            value = "";
            params["direction"] = "Input";
            paramOptions["direction"] = {"Input", "Output", "BiDir"};
            break;
        case ComponentType::PowerPort:
            name = "POWER?";
            value = "";
            params["railName"] = "VDD";
            params["direction"] = "Power";
            paramOptions["direction"] = {"Power", "Input", "Output", "BiDir"};
            break;
        case ComponentType::Generic:
            name = "U?";
            value = "Model";
            break;
        default: 
            name = "U?"; 
            value = "0"; 
            break;
    }
}

// ============================================================================
// ICPackage: Load pins dynamically from child schematic
// ============================================================================
void VisualComponent::loadPinsFromSchematic(const QString& schPath) {
    if (type != ComponentType::ICPackage) return;
    
    QFile f(schPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[ICPackage] Failed to open schematic:" << schPath;
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    
    if (!doc.isObject()) {
        qDebug() << "[ICPackage] Invalid JSON in:" << schPath;
        return;
    }
    
    QJsonArray components = doc.object()["components"].toArray();
    
    // Extract display name from metadata if available
    QJsonObject meta = doc.object()["metadata"].toObject();
    if (meta.contains("displayName")) {
        displayName = meta["displayName"].toString();
    } else if (meta.contains("name")) {
        displayName = meta["name"].toString();
    }
    
    pinList.clear();
    
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        QString ctype = c["type"].toString();
        if (ctype != "Pin" && ctype != "PowerPort") continue;

        PinInfo pin;
        pin.name = c["name"].toString();
        // PowerPort pins are always power-type
        if (ctype == "PowerPort") {
            pin.direction = PinInfo::Direction::Power;
            pin.isPower   = true;
        }
        
        QJsonObject props = c["properties"].toObject();
        
        // Direction
        QString dir = props["direction"].toString().toLower();
        if (dir == "input") pin.direction = PinInfo::Direction::Input;
        else if (dir == "output") pin.direction = PinInfo::Direction::Output;
        else if (dir == "bidir" || dir == "inout") pin.direction = PinInfo::Direction::BiDir;
        else if (dir == "power") pin.direction = PinInfo::Direction::Power;
        else pin.direction = PinInfo::Direction::Input;
        
        // Bus detection: NAME[INDEX] pattern (e.g., "D[3]", "ADDR[15]")
        static QRegularExpression busRe(R"((\w+)\[(\d+)\])");
        auto match = busRe.match(pin.name);
        if (match.hasMatch()) {
            pin.busGroup = match.captured(1);
            pin.busIndex = match.captured(2).toInt();
        }
        
        // Power pin detection
        QString upper = pin.name.toUpper();
        pin.isPower = (upper == "VDD" || upper == "VSS" || 
                      upper == "VCC" || upper == "GND" ||
                      upper == "AVDD" || upper == "AVSS" ||  // Analog supplies
                      upper == "DVDD" || upper == "DVSS" ||  // Digital supplies
                      upper.startsWith("PWR") || upper.startsWith("GND"));
        if (pin.isPower) pin.direction = PinInfo::Direction::Power;
        
        // Domain hints from properties
        pin.domain = props["domain"].toString();
        pin.impedance = props["impedance"].toString();
        pin.netType = props["netType"].toString();
        
        pinList.append(pin);
    }
    
    qDebug() << "[ICPackage] Loaded" << pinList.size() << "pins from" << schPath;

    // Re-validate symbol pins against updated schematic pin list
    validateSymbolPins();

    // Trigger geometry recalculation
    prepareGeometryChange();
    update();
}

QRectF VisualComponent::boundingRect() const {
    if (type == ComponentType::Probe) {
        return QRectF(-20, -20, 40, 40);
    }
    if (type == ComponentType::ICPackage) {
        // Dynamic size based on loaded pins
        if (pinList.isEmpty()) {
            return QRectF(-60, -50, 120, 100); // Default size
        }
        
        // Count pins by side
        int inputCount = 0, outputCount = 0, powerCount = 0;
        int maxNameLen = 4; // Minimum label width
        
        for (const auto& pin : pinList) {
            maxNameLen = std::max(maxNameLen, static_cast<int>(pin.name.length()));
            if (pin.isPower || pin.direction == PinInfo::Direction::Power) {
                powerCount++;
            } else if (pin.direction == PinInfo::Direction::Input) {
                inputCount++;
            } else {
                outputCount++;
            }
        }
        
        // Height: based on max pins on left/right side + power rails
        int sideMax = std::max(inputCount, outputCount);
        int verticalPins = std::max(1, sideMax);
        int height = 60 + verticalPins * 25; // Header + pins
        
        // Width: based on longest pin name
        int width = std::max(120, 40 + maxNameLen * 8 + 40);
        
        // Center the rect
        return QRectF(-width/2.0, -height/2.0, width, height);
    }
    if (type == ComponentType::Generic) {
        // Dynamic size based on terminals
        auto terms = getTerminals();
        if (terms.isEmpty()) return QRectF(-30, -30, 60, 60);
        
        QRectF r(-30, -30, 60, 60); // Min body size
        for(const auto& p : terms) {
            if (p.x() < r.left()) r.setLeft(p.x());
            if (p.x() > r.right()) r.setRight(p.x());
            if (p.y() < r.top()) r.setTop(p.y());
            if (p.y() > r.bottom()) r.setBottom(p.y());
        }
        return r.adjusted(-5,-5,5,5);
    }
    if (type == ComponentType::SubCircuit) {
        // Size accounts for body (-40,-40,80,80) + terminal stubs extending beyond
        // Terminals can be at x=-60 or x=60 (left/right) or y=-80/+80 (top/bottom)
        QRectF rect(-40, -40, 80, 80);
        auto terms = getTerminals();
        for (const auto& t : terms) {
            if (t.x() < rect.left())   rect.setLeft(t.x() - 5);
            if (t.x() > rect.right())  rect.setRight(t.x() + 5);
            if (t.y() < rect.top())    rect.setTop(t.y() - 5);
            if (t.y() > rect.bottom()) rect.setBottom(t.y() + 5);
        }
        return rect.adjusted(-3, -3, 3, 3); // Small margin for dots/labels
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
    // If a symbol definition is loaded, use its pin positions
    if (symbolDef.has_value()) {
        QVector<QPointF> pts;
        for (const auto& sp : symbolDef->pins)
            pts.push_back(sp.pos);
        return pts;
    }
    // Terminals MUST land on 20px grid (GRID_STEP)
    QVector<QPointF> t;
    switch(type) {
        case ComponentType::Resistor:
        case ComponentType::Inductor:
            t << QPointF(-40, 0) << QPointF(40, 0);
            break;
        case ComponentType::Capacitor:
        case ComponentType::Diode:
        case ComponentType::PDK_Capacitor:
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
        case ComponentType::PDK_NFET:
        case ComponentType::PDK_PFET:
             // Gate (-20, 0), Drain (20, -20), Source (20, 20), Bulk (0, 0)
             t << QPointF(-20, 0) << QPointF(20, -20) << QPointF(20, 20) << QPointF(0, 0); 
             break;
        case ComponentType::PDK_Resistor:
             // A (-40, 0), B (40, 0), SUB (0, 20)
             t << QPointF(-40, 0) << QPointF(40, 0) << QPointF(0, 20);
             break;
        case ComponentType::SubCircuit: {
            const auto* def = StandardCells::StandardCellLibrary::instance().get(subCircuitName.toStdString());
            if (!def) return t;
            int total = (int)def->pinNames.size();
            for (int i=0; i<total; ++i) {
                QString pname = QString::fromStdString(def->pinNames[i]).toUpper();
                QPointF pos;
                if (pname == "GND" || pname == "VSS") { 
                    t << QPointF(0, 80); // Bottom center
                } else if (pname == "VDD" || pname == "VCC") { 
                    t << QPointF(0, -80); // Top center
                } else if (pname.startsWith("IN") || pname == "VIN" || pname == "A" || pname == "B") { 
                    t << QPointF(-60, -40 + (i%4)*40); // Left side
                } else if (pname.startsWith("OUT") || pname == "VOUT") {
                    t << QPointF(60, 0); // Right side
                } else {
                    t << QPointF(-60, 0); // Default left
                }
            }
            }
            break;
        case ComponentType::Generic:  // two pins until OSDI module terminals reach the GUI
            t << QPointF(-40, 0) << QPointF(40, 0);
            break;
        case ComponentType::Probe:
             return {};
        case ComponentType::VCVS:
        case ComponentType::VCCS:
        case ComponentType::CCVS:
        case ComponentType::CCCS:
            // 4 terminals: 2 control (-40, -20/20), 2 output (40, -20/20)
            t << QPointF(-40, -20) << QPointF(-40, 20) << QPointF(40, -20) << QPointF(40, 20);
            break;
        
        // Power Rails - Single terminal (connection point)
        case ComponentType::PDK_VDD:
        case ComponentType::PDK_VDDIO:
        case ComponentType::PDK_VBB:
            t << QPointF(0, 20);  // Output at bottom of symbol
            break;
        case ComponentType::PDK_VSS:
            t << QPointF(0, -20); // Output at top of symbol (ground connects upward)
            break;
            
        case ComponentType::Pin:
            t << QPointF(0, 0); // Connection point at center of diamond
            break;

        case ComponentType::PowerPort:
            t << QPointF(0, 20); // Single connection point below symbol (like VDD rail)
            break;

        case ComponentType::ICPackage: {
            if (pinList.isEmpty()) {
                // Fallback: single center terminal
                t << QPointF(0, 0);
                break;
            }
            
            QRectF bounds = boundingRect();
            
            // Categorize pins
            QVector<int> inputPins, outputPins, powerVddPins, powerGndPins;
            for (int i = 0; i < pinList.size(); ++i) {
                const auto& pin = pinList[i];
                QString upper = pin.name.toUpper();
                
                if (pin.isPower || pin.direction == PinInfo::Direction::Power) {
                    if (upper.contains("VDD") || upper.contains("VCC") || 
                        upper.contains("AVDD") || upper.contains("DVDD")) {
                        powerVddPins.append(i);
                    } else {
                        powerGndPins.append(i);
                    }
                } else if (pin.direction == PinInfo::Direction::Input) {
                    inputPins.append(i);
                } else {
                    outputPins.append(i);
                }
            }
            
            // Place input pins on left (snap to grid)
            double leftX = std::round(bounds.left() / 20.0) * 20.0;
            double startY = std::round((bounds.top() + 35) / 20.0) * 20.0;
            for (int i = 0; i < inputPins.size(); ++i) {
                t << QPointF(leftX, startY + i * 20);
            }
            
            // Place output pins on right
            double rightX = std::round(bounds.right() / 20.0) * 20.0;
            for (int i = 0; i < outputPins.size(); ++i) {
                t << QPointF(rightX, startY + i * 20);
            }
            
            // Place VDD on top
            double topY = std::round(bounds.top() / 20.0) * 20.0;
            double powerStartX = std::round((bounds.left() + 40) / 20.0) * 20.0;
            for (int i = 0; i < powerVddPins.size(); ++i) {
                t << QPointF(powerStartX + i * 20, topY);
            }
            
            // Place GND on bottom
            double bottomY = std::round(bounds.bottom() / 20.0) * 20.0;
            for (int i = 0; i < powerGndPins.size(); ++i) {
                t << QPointF(powerStartX + i * 20, bottomY);
            }
            
            break;
        }
            
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
        // Power Rails - Compact bounding box
        case ComponentType::PDK_VDD:
        case ComponentType::PDK_VDDIO:
        case ComponentType::PDK_VBB:
            path.addRect(-15, -30, 30, 50);
            break;
        case ComponentType::PDK_VSS:
            path.addRect(-15, -20, 30, 35);
            break;
        case ComponentType::Generic:
            {
                QRectF b = boundingRect();
                path.addRect(b.adjusted(10, 10, -10, -10));
            }
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
    // Phase 11.6 Optimized Rendering
    if (m_bodyPen.style() == Qt::NoPen) updateCachedPens(); // Safety check

    if (ghostState == GhostState::Preview || ghostState == GhostState::Focused) {
        int alpha = static_cast<int>(255 * ghostConfidence);
        
        QPen p = m_ghostPen;
        p.setColor(QColor(p.color().red(), p.color().green(), p.color().blue(), alpha));
        
        painter->setPen(p);
        painter->setBrush(m_ghostBrush);
    } else {
        painter->setPen(m_bodyPen);
        painter->setBrush(m_bodyBrush);
        
        // Probe logic
        if (type == ComponentType::Probe && probePositiveTip && probeNegativeTip) {
             painter->save();
             QPen dashPen(acutesim::gui::styling::ThemeManager::instance().getColor("schematic.probe_dash"), 1, Qt::DashLine);
             painter->setPen(dashPen);
             painter->drawLine(QPointF(0,0), probePositiveTip->pos());
             painter->drawLine(QPointF(0,0), probeNegativeTip->pos());
             painter->restore();
             painter->setPen(m_bodyPen); // Restore body pen
        }
    }
        
        // Render "Why?" Tooltip if Focused
        if (ghostState == GhostState::Focused && !ghostWhy.isEmpty()) {
            painter->save();
            // Draw floating tooltip above component
            QRectF tipRect = boundingRect();
            tipRect.moveTop(tipRect.top() - 30);
            tipRect.setHeight(25);
            
            painter->setBrush(QColor(40, 40, 40, 220));
            painter->setPen(QPen(m_ghostPen.color(), 1));
            painter->drawRoundedRect(tipRect, 4, 4);
            
            painter->setPen(Qt::white);
            painter->drawText(tipRect, Qt::AlignCenter, "Why: " + ghostWhy); // Simple for now
            
            // "Tab to Accept" Badge
            QRectF badgeRect = boundingRect();
            badgeRect.moveBottom(badgeRect.bottom() + 30);
            badgeRect.setHeight(20);
            painter->setBrush(QColor(30, 100, 30, 220));
            painter->drawRoundedRect(badgeRect, 4, 4);
            painter->drawText(badgeRect, Qt::AlignCenter, "TAB to Accept");
            
            painter->restore();
        }
    
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
            painter->drawLine(-5, -10, -5, 10); // Anode
            {
                // Triangle
                QPointF poly[] = {{10, 0}, {-5, -10}, {-5, 10}};
                painter->drawPolygon(poly, 3);
            }
            painter->drawLine(10, -10, 10, 10); // Cathode Bar
            painter->drawLine(-20, 0, -5, 0);
            painter->drawLine(10, 0, 20, 0);
            break;
            
        case ComponentType::Mosfet:
        case ComponentType::PDK_NFET:
        case ComponentType::PDK_PFET:
            // Draw NMOS/PMOS Symbol
            {
                bool isPMOS = (type == ComponentType::PDK_PFET || value.contains("PMOS") || value.contains("pfet"));
                
                // Leads
                painter->drawLine(-20, 0, -10, 0); // Gate lead
                
                // Gate Plate
                painter->drawLine(-10, -15, -10, 15);
                
                // Channel Plates (3 segments - solid for PDK)
                painter->drawLine(0, -15, 0, 15);
                
                // Bulk connection (PDK-specific or generic)
                painter->drawLine(0, 0, 10, 0); // Bulk connection
                
                // Arrow
                if (isPMOS) {
                    painter->drawLine(5, 0, 2, -3);
                    painter->drawLine(5, 0, 2, 3);
                    painter->drawEllipse(-12, -2, 4, 4);
                } else {
                    painter->drawLine(0, 0, 5, -3);
                    painter->drawLine(0, 0, 5, 3);
                }
                
                // Connect D/S to channel
                painter->drawLine(10, -15, 0, -15);
                painter->drawLine(10, 15, 0, 15);
                
                // Vertical D/S lines to terminals (20, -20) and (20, 20)
                painter->drawLine(10, -15, 20, -15);
                painter->drawLine(20, -15, 20, -20);
                painter->drawLine(10, 15, 20, 15);
                painter->drawLine(20, 15, 20, 20);
            }
            break;
            
        case ComponentType::PDK_Resistor:
            painter->drawRect(-20, -8, 40, 16);
            painter->drawLine(-40, 0, -20, 0); 
            painter->drawLine(20, 0, 40, 0);
            painter->drawLine(0, 8, 0, 20); // Substrate terminal
            break;
            
        case ComponentType::PDK_Capacitor:
            painter->drawLine(-5, -20, -5, 20);
            painter->drawLine(5, -20, 5, 20);
            painter->drawLine(-20, 0, -5, 0);
            painter->drawLine(5, 0, 20, 0);
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

        case ComponentType::Generic: {
            QRectF b = boundingRect().adjusted(10, 10, -10, -10);
            painter->drawRect(b);
            painter->drawText(b, Qt::AlignCenter, subCircuitName);
            
            auto terms = getTerminals();
            for(int i=0; i<terms.size(); ++i) {
                QPointF p = terms[i];
                if (p.x() < 0) painter->drawLine(p, QPointF(b.left(), p.y()));
                else painter->drawLine(p, QPointF(b.right(), p.y()));
                
                const QString label = QString::number(i+1);
                
                QPointF textPos = (p.x() < 0) ? QPointF(b.left()+5, p.y()+5) : QPointF(b.right()-15, p.y()+5);
                painter->drawText(textPos, label);
            }
            break;
        }

        case ComponentType::SubCircuit: {
        const auto* def = StandardCells::StandardCellLibrary::instance().get(subCircuitName.toStdString());
        QRectF rect(-40, -40, 80, 80);

        // Body
        painter->drawRect(rect);
        QString displayText = subCircuitName.isEmpty() ? "SUB" : subCircuitName;
        painter->drawText(rect, Qt::AlignCenter, displayText);

        // Wire stubs from each terminal to nearest body edge, with pin labels
        auto terms = getTerminals();
        for (int i = 0; i < terms.size(); ++i) {
            QPointF t = terms[i];

            // Connection dot
            painter->setBrush(Qt::white);
            painter->drawEllipse(t, 3, 3);

            // Compute nearest body edge point
            QPointF edge = t;
            if (t.x() <= rect.left())        edge = QPointF(rect.left(), t.y());
            else if (t.x() >= rect.right())  edge = QPointF(rect.right(), t.y());
            else if (t.y() <= rect.top())    edge = QPointF(t.x(), rect.top());
            else if (t.y() >= rect.bottom()) edge = QPointF(t.x(), rect.bottom());

            // Wire stub line
            if (edge != t) painter->drawLine(t, edge);

            // Pin label (from standard cell definition)
            if (def && i < (int)def->pinNames.size()) {
                QString label = QString::fromStdString(def->pinNames[i]);
                QFont labelFont = painter->font();
                labelFont.setPointSize(7);
                painter->setFont(labelFont);

                if (t.x() <= rect.left()) {
                    painter->drawText(QRectF(rect.left() + 3, t.y() - 8, 35, 16),
                                      Qt::AlignLeft | Qt::AlignVCenter, label);
                } else if (t.x() >= rect.right()) {
                    painter->drawText(QRectF(rect.right() - 38, t.y() - 8, 35, 16),
                                      Qt::AlignRight | Qt::AlignVCenter, label);
                } else if (t.y() <= rect.top()) {
                    painter->drawText(QRectF(t.x() - 18, rect.top() + 2, 36, 14),
                                      Qt::AlignCenter, label);
                } else if (t.y() >= rect.bottom()) {
                    painter->drawText(QRectF(t.x() - 18, rect.bottom() - 16, 36, 14),
                                      Qt::AlignCenter, label);
                }
            }
        }
        break;
    }
    case ComponentType::Pin: {
        // Draw a Diamond
        QPolygonF diamond;
        diamond << QPointF(-10, 0) << QPointF(0, -10) << QPointF(10, 0) << QPointF(0, 10);
        painter->drawPolygon(diamond);
        // Draw Name
        painter->drawText(QRectF(-20, -25, 40, 15), Qt::AlignCenter, name);
        break;
    }

    case ComponentType::PowerPort: {
        // Triangle rail glyph pointing up + rail name label
        QString railName = params.value("railName", name);
        painter->setPen(QPen(QColor("#FF5555"), 2));  // Red for power rails
        painter->setBrush(QColor("#FF5555"));
        QPolygonF tri;
        tri << QPointF(-12, 0) << QPointF(12, 0) << QPointF(0, -18);
        painter->drawPolygon(tri);
        // Vertical lead from apex down to connection point
        painter->drawLine(0, 0, 0, 20);
        // Rail name above triangle
        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPointSize(8);
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(QRectF(-20, -32, 40, 12), Qt::AlignCenter, railName);
        break;
    }

    case ComponentType::ICPackage: {
        // If a custom symbol is loaded, use it instead of the generic box
        if (symbolDef.has_value()) {
            for (const auto& g : symbolDef->graphics) {
                if (g.debugOnly) continue; // skip debug-only primitives
                if (g.type == SymbolGraphic::Type::Polygon)
                    painter->drawPolygon(g.points);
                else if (g.type == SymbolGraphic::Type::Rect)
                    painter->drawRect(g.rect);
            }
            // Draw labels with token substitution
            for (const auto& lbl : symbolDef->labels) {
                QString text = lbl.text;
                text.replace("{NAME}", name);
                text.replace("{VALUE}", value);
                painter->drawText(lbl.pos, text);
            }
            // Draw pin stubs (short lines from connection point outward)
            for (const auto& sp : symbolDef->pins) {
                QPointF stub = pinStubEndpoint(sp);
                painter->drawLine(sp.pos, stub);
            }
            break;
        }

        QRectF bounds = boundingRect();
        QRectF body = bounds.adjusted(10, 0, -10, 0);

        // Body fill - always draw this so component is visible
        painter->setBrush(QColor("#2D2D30"));
        painter->setPen(QPen(Qt::white, 2));
        painter->drawRect(body);
        
        // Header bar with name
        QRectF header(body.left(), body.top(), body.width(), 22);
        painter->setBrush(QColor("#007ACC"));
        painter->drawRect(header);
        
        QString title = displayName.isEmpty() ? 
            (subCircuitName.isEmpty() ? name : subCircuitName) : displayName;
        painter->setPen(Qt::white);
        QFont headerFont = painter->font();
        headerFont.setBold(true);
        headerFont.setPointSize(9);
        painter->setFont(headerFont);
        painter->drawText(header, Qt::AlignCenter, title);
        
        // If no pins loaded, show placeholder message
        if (pinList.isEmpty()) {
            painter->setPen(QColor("#888888"));
            QFont placeholderFont = painter->font();
            placeholderFont.setPointSize(8);
            placeholderFont.setItalic(true);
            painter->setFont(placeholderFont);
            
            if (sourceFilePath.isEmpty()) {
                painter->drawText(body.adjusted(5, 25, -5, -5), 
                                Qt::AlignCenter | Qt::TextWordWrap, 
                                "No source file\nspecified");
            } else {
                painter->drawText(body.adjusted(5, 25, -5, -5), 
                                Qt::AlignCenter | Qt::TextWordWrap, 
                                "Source: " + sourceFilePath + "\n(not found)");
            }
            break;
        }
        
        // Reset font for labels
        QFont labelFont = painter->font();
        labelFont.setBold(false);
        labelFont.setPointSize(8);
        painter->setFont(labelFont);
        
        // Draw pins with labels
        auto terms = getTerminals();
        
        // Build index mapping: terminals are ordered input, output, vdd, gnd
        QVector<int> inputPins, outputPins, powerVddPins, powerGndPins;
        for (int i = 0; i < pinList.size(); ++i) {
            const auto& pin = pinList[i];
            QString upper = pin.name.toUpper();
            if (pin.isPower || pin.direction == PinInfo::Direction::Power) {
                if (upper.contains("VDD") || upper.contains("VCC") || 
                    upper.contains("AVDD") || upper.contains("DVDD")) {
                    powerVddPins.append(i);
                } else {
                    powerGndPins.append(i);
                }
            } else if (pin.direction == PinInfo::Direction::Input) {
                inputPins.append(i);
            } else {
                outputPins.append(i);
            }
        }
        
        int termIdx = 0;
        
        // Input pins (left side)
        painter->setPen(Qt::white);
        for (int idx : inputPins) {
            if (termIdx >= terms.size()) break;
            QPointF pos = terms[termIdx++];
            
            // Connection dot
            painter->setBrush(QColor("#4EC9B0")); // Teal for inputs
            painter->drawEllipse(pos, 4, 4);
            
            // Lead line to body
            painter->drawLine(pos, QPointF(body.left(), pos.y()));
            
            // Label (inside body, right of dot)
            QString label = pinList[idx].name;
            painter->drawText(QRectF(body.left() + 5, pos.y() - 8, 50, 16),
                            Qt::AlignLeft | Qt::AlignVCenter, label);
        }
        
        // Output pins (right side)
        for (int idx : outputPins) {
            if (termIdx >= terms.size()) break;
            QPointF pos = terms[termIdx++];
            
            painter->setBrush(QColor("#DCDCAA")); // Yellow for outputs
            painter->drawEllipse(pos, 4, 4);
            
            painter->drawLine(pos, QPointF(body.right(), pos.y()));
            
            QString label = pinList[idx].name;
            painter->drawText(QRectF(body.right() - 55, pos.y() - 8, 50, 16),
                            Qt::AlignRight | Qt::AlignVCenter, label);
        }
        
        // Power VDD pins (top)
        for (int idx : powerVddPins) {
            if (termIdx >= terms.size()) break;
            QPointF pos = terms[termIdx++];
            
            painter->setBrush(QColor("#FF5555")); // Red for VDD
            painter->drawEllipse(pos, 4, 4);
            
            painter->drawLine(pos, QPointF(pos.x(), body.top()));
            
            QString label = pinList[idx].name;
            painter->drawText(QRectF(pos.x() - 20, body.top() + 24, 40, 14),
                            Qt::AlignCenter, label);
        }
        
        // Power GND pins (bottom)
        for (int idx : powerGndPins) {
            if (termIdx >= terms.size()) break;
            QPointF pos = terms[termIdx++];
            
            painter->setBrush(QColor("#569CD6")); // Blue for GND
            painter->drawEllipse(pos, 4, 4);
            
            painter->drawLine(pos, QPointF(pos.x(), body.bottom()));
            
            QString label = pinList[idx].name;
            painter->drawText(QRectF(pos.x() - 20, body.bottom() - 18, 40, 14),
                            Qt::AlignCenter, label);
        }
        
        // Drill-down indicator if source file exists
        if (!sourceFilePath.isEmpty()) {
            painter->setPen(QPen(QColor("#569CD6"), 1));
            QFont indicatorFont = painter->font();
            indicatorFont.setPointSize(7);
            painter->setFont(indicatorFont);
            painter->drawText(body.adjusted(0, 0, 0, -3), 
                            Qt::AlignBottom | Qt::AlignHCenter, "▼ dbl-click");
        }
        
        break;
    }
             
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
                
                // Straight control leads
                painter->setPen(QPen(Qt::gray, 1, Qt::DashLine));
                painter->drawLine(-20, 0, -30, 0); // Control bus point
                painter->drawLine(-30, 0, -30, -20);
                painter->drawLine(-30, 0, -30, 20);
                painter->drawLine(-30, -20, -40, -20);
                painter->drawLine(-30, 20, -40, 20);
                
                // Straight output leads
                painter->setPen(QPen(acutesim::gui::styling::ThemeManager::instance().getColor("ui.text"), 2));
                painter->drawLine(20, 0, 30, 0); // Output bus point
                painter->drawLine(30, 0, 30, -20);
                painter->drawLine(30, 0, 30, 20);
                painter->drawLine(30, -20, 40, -20);
                painter->drawLine(30, 20, 40, 20);
            }
            break;

        // =====================================================================
        // POWER RAILS (PDK Domain-Aware)
        // =====================================================================
        case ComponentType::PDK_VDD:
            // VDD Symbol: Horizontal bar with up-arrow
            {
                painter->setPen(QPen(QColor("#FF5555"), 2)); // Red for VDD
                painter->drawLine(-15, -15, 15, -15);        // Horizontal bar
                painter->drawLine(0, -15, 0, 20);            // Vertical lead down
                
                // Up arrow on bar
                painter->drawLine(0, -15, -5, -10);
                painter->drawLine(0, -15, 5, -10);
                
                // Label
                painter->setPen(Qt::white);
                QFont f = painter->font();
                f.setPointSize(8);
                f.setBold(true);
                painter->setFont(f);
                painter->drawText(QRectF(-20, -30, 40, 12), Qt::AlignCenter, "VDD");
            }
            break;
            
        case ComponentType::PDK_VSS:
            // VSS Symbol: Ground variant with multiple horizontal lines
            {
                painter->setPen(QPen(QColor("#55AAFF"), 2)); // Blue for VSS/GND
                painter->drawLine(0, -20, 0, 0);             // Vertical lead up
                painter->drawLine(-15, 0, 15, 0);            // Main bar
                painter->drawLine(-10, 5, 10, 5);            // Second bar
                painter->drawLine(-5, 10, 5, 10);            // Third bar (triangle hint)
                
                // Label
                painter->setPen(Qt::white);
                QFont f = painter->font();
                f.setPointSize(8);
                f.setBold(true);
                painter->setFont(f);
                painter->drawText(QRectF(-20, 12, 40, 12), Qt::AlignCenter, "VSS");
            }
            break;
            
        case ComponentType::PDK_VDDIO:
            // VDDIO Symbol: Bar with IO label
            {
                painter->setPen(QPen(QColor("#FFAA55"), 2)); // Orange for VDDIO
                painter->drawLine(-15, -15, 15, -15);        // Horizontal bar
                painter->drawLine(0, -15, 0, 20);            // Vertical lead down
                
                // Up arrow
                painter->drawLine(0, -15, -5, -10);
                painter->drawLine(0, -15, 5, -10);
                
                // Label
                painter->setPen(Qt::white);
                QFont f = painter->font();
                f.setPointSize(7);
                f.setBold(true);
                painter->setFont(f);
                painter->drawText(QRectF(-25, -30, 50, 12), Qt::AlignCenter, "VDDIO");
            }
            break;
            
        case ComponentType::PDK_VBB:
            // VBB Symbol: Bar with bias indicator
            {
                painter->setPen(QPen(QColor("#AA55FF"), 2)); // Purple for body bias
                painter->drawLine(-12, -12, 12, -12);        // Horizontal bar
                painter->drawLine(0, -12, 0, 20);            // Vertical lead down
                
                // Tilde for variable bias
                painter->drawArc(-6, -18, 6, 6, 0, 180 * 16);
                painter->drawArc(0, -18, 6, 6, 180 * 16, 180 * 16);
                
                // Label
                painter->setPen(Qt::white);
                QFont f = painter->font();
                f.setPointSize(8);
                f.setBold(true);
                painter->setFont(f);
                painter->drawText(QRectF(-20, -35, 40, 12), Qt::AlignCenter, "VBB");
            }
            break;


        default:
            painter->setPen(QPen(Qt::white, 2));
            painter->drawRect(-15, -15, 30, 30);
            break;
    }
    
    // ── Symbol validation overlay ───────────────────────────────────────
    if (m_symbolValidation == SymbolValidation::GhostPin) {
        // Red error box — netlisting is blocked
        painter->setPen(QPen(QColor("#FF3333"), 2, Qt::DashLine));
        painter->setBrush(QColor(255, 0, 0, 30));
        painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
        painter->setPen(QColor("#FF3333"));
        painter->setFont(QFont());
        painter->drawText(boundingRect().topLeft() + QPointF(4, 14), "! GHOST PIN");
    } else if (m_symbolValidation == SymbolValidation::HiddenPin) {
        // Yellow warning badge — netlisting still works
        painter->setPen(QPen(QColor("#FFAA00"), 2));
        painter->setBrush(QColor(255, 170, 0, 50));
        QRectF badge(boundingRect().right() - 18, boundingRect().top(), 16, 16);
        painter->drawEllipse(badge);
        painter->setFont(QFont());
        painter->drawText(badge, Qt::AlignCenter, "!");
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
    
    // Quick Preview tooltip (Phase 1.7)
    // Draw estimated voltage badge for probes when hasQuickPreview is true
    if (hasQuickPreview && (type == ComponentType::Probe || 
                            type == ComponentType::VoltageSource)) {
        painter->save();
        
        // Yellow badge background
        QRectF badgeRect(-25, -35, 50, 18);
        painter->setBrush(QColor(255, 200, 50, 220)); // Yellow, semi-transparent
        painter->setPen(QPen(QColor(180, 140, 0), 1));
        painter->drawRoundedRect(badgeRect, 4, 4);
        
        // Draw approximate symbol and voltage
        painter->setPen(Qt::black);
        QFont font = painter->font();
        font.setPointSize(9);
        font.setBold(true);
        painter->setFont(font);
        
        QString voltageText = QString::fromUtf8("≈") + QString::number(quickPreviewVoltage, 'g', 3) + "V";
        painter->drawText(badgeRect, Qt::AlignCenter, voltageText);
        
        painter->restore();
    }
}

// ============================================================================
// HOVER EVENTS (Phase 1.7 Quick Preview)
// ============================================================================

void VisualComponent::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    
    // Only show quick preview for probes and sources
    if (type == ComponentType::Probe || 
        type == ComponentType::VoltageSource) {
        
        // Set cursor to indicate hoverable
        setCursor(Qt::CrossCursor);
        
        // Request quick preview from scene (GridScene will call setQuickPreview)
        // This is handled by the scene's event filter or direct call
        // For now, just mark as "hover active" for visual feedback
        update();
    }
    
    QGraphicsItem::hoverEnterEvent(event);
}

void VisualComponent::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    
    // Clear quick preview on hover exit
    clearQuickPreview();
    unsetCursor();
    
    QGraphicsItem::hoverLeaveEvent(event);
}

// ============================================================================
// SYMBOL (.sym) FILE SUPPORT
// ============================================================================
bool VisualComponent::loadSymbol(const QString& symPath) {
    QFile f(symPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;
    QJsonObject obj = doc.object();

    SymbolDef def;

    // Parse meta section
    QJsonObject meta = obj["meta"].toObject();
    def.symbolName        = meta["name"].toString();
    def.designatorPrefix  = meta["designator_prefix"].toString();
    def.version           = meta["version"].toString();

    // Parse graphics[] — typed primitives (polygon, rect)
    for (const auto& gVal : obj["graphics"].toArray()) {
        QJsonObject g = gVal.toObject();
        SymbolGraphic sg;
        sg.debugOnly = (g["condition"].toString() == "debug_mode");

        QString gtype = g["type"].toString();
        if (gtype == "polygon") {
            sg.type = SymbolGraphic::Type::Polygon;
            for (const auto& pt : g["points"].toArray()) {
                QJsonArray xy = pt.toArray();
                sg.points << QPointF(xy[0].toDouble(), xy[1].toDouble());
            }
        } else if (gtype == "rect") {
            sg.type = SymbolGraphic::Type::Rect;
            sg.rect = QRectF(g["x"].toDouble(), g["y"].toDouble(),
                             g["w"].toDouble(), g["h"].toDouble());
        } else {
            continue; // skip unknown types
        }
        def.graphics.push_back(sg);
    }

    // Legacy: parse flat "shape" array as a polygon for backwards compat
    if (def.graphics.isEmpty() && obj.contains("shape")) {
        SymbolGraphic sg;
        sg.type = SymbolGraphic::Type::Polygon;
        for (const auto& pt : obj["shape"].toArray()) {
            QJsonArray xy = pt.toArray();
            sg.points << QPointF(xy[0].toDouble(), xy[1].toDouble());
        }
        if (!sg.points.isEmpty())
            def.graphics.push_back(sg);
    }

    // Parse pins[] — with orientation, length, electrical
    for (const auto& pinVal : obj["pins"].toArray()) {
        QJsonObject p = pinVal.toObject();
        SymbolPin sp;
        sp.name        = p["name"].toString();
        sp.pos         = QPointF(p["x"].toDouble(), p["y"].toDouble());
        sp.orientation = p["orientation"].toString("left");
        sp.length      = p["length"].toInt(10);
        sp.electrical  = p["electrical"].toString("signal");
        def.pins.push_back(sp);
    }

    // Parse labels[] — with optional type field
    for (const auto& lblVal : obj["labels"].toArray()) {
        QJsonObject l = lblVal.toObject();
        SymbolDef::Label lbl;
        lbl.text = l["text"].toString();
        lbl.pos  = QPointF(l["x"].toDouble(), l["y"].toDouble());
        lbl.type = l["type"].toString("value");
        def.labels.push_back(lbl);
    }

    symbolDef = def;

    // Deferred pin validation — sets m_symbolValidation
    validateSymbolPins();
    update();
    return true;
}

// ============================================================================
// SYMBOL PIN STUB ENDPOINT HELPER
// ============================================================================
QPointF VisualComponent::pinStubEndpoint(const SymbolPin& sp) const {
    QPointF offset;
    if (sp.orientation == "left")        offset = QPointF(-sp.length, 0);
    else if (sp.orientation == "right")  offset = QPointF(sp.length, 0);
    else if (sp.orientation == "up")     offset = QPointF(0, -sp.length);
    else if (sp.orientation == "down")   offset = QPointF(0, sp.length);
    return sp.pos + offset;
}

// ============================================================================
// THREE-STATE PIN VALIDATION
// ============================================================================
bool VisualComponent::validateSymbolPins() {
    if (!symbolDef.has_value()) {
        m_symbolValidation = SymbolValidation::Valid;
        return true;
    }

    // Ghost pin: symbol has it, schematic doesn't → RED, netlisting blocked
    for (const auto& sp : symbolDef->pins) {
        bool inSch = std::any_of(pinList.begin(), pinList.end(),
            [&](const PinInfo& pi){ return pi.name == sp.name; });
        if (!inSch) {
            m_symbolValidation = SymbolValidation::GhostPin;
            qWarning() << "[SymbolValidation] Ghost pin:" << sp.name;
            return false;
        }
    }

    // Hidden pin: schematic has it, symbol doesn't → YELLOW, netlisting ok
    for (const auto& pi : pinList) {
        bool inSym = std::any_of(symbolDef->pins.begin(), symbolDef->pins.end(),
            [&](const SymbolPin& sp){ return sp.name == pi.name; });
        if (!inSym) {
            m_symbolValidation = SymbolValidation::HiddenPin;
            qWarning() << "[SymbolValidation] Hidden pin:" << pi.name;
            return true;
        }
    }

    m_symbolValidation = SymbolValidation::Valid;
    return true;
}

void VisualComponent::keyPressEvent(QKeyEvent* event) {
    if (ghostState != GhostState::None) {
        if (event->key() == Qt::Key_Tab) {
            // Accept Ghost
            setGhostState(GhostState::Accepted);
            // In real flow, this would trigger signals to commit to netlist
            // For now, we just visually transition
            setGhostState(GhostState::None); 
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Escape) {
            // Reject Ghost
            setGhostState(GhostState::Rejected);
            // Scene would typically remove it (managed by controller)
            setVisible(false);
            event->accept();
            return;
        }
    }
    QGraphicsItem::keyPressEvent(event);
}
