#include "schematic_editor.h"
#include <QDebug>

// ============================================================================
// AI API IMPLEMENTATION
// ============================================================================

VisualComponent* SchematicEditor::findComponent(const QString& id) {
    if (!scene) return nullptr;
    for (auto* item : scene->items()) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->name == id) return c;
        }
    }
    return nullptr;
}

QString SchematicEditor::generateUniqueId(ComponentType type) {
    QString prefix;
    switch(type) {
        case ComponentType::Resistor: prefix = "R"; break;
        case ComponentType::Capacitor: prefix = "C"; break;
        case ComponentType::Inductor: prefix = "L"; break;
        case ComponentType::VoltageSource: prefix = "V"; break;

        case ComponentType::Diode: prefix = "D"; break;
        case ComponentType::Mosfet: prefix = "M"; break;
        case ComponentType::Bjt: prefix = "Q"; break;
        case ComponentType::Ground: prefix = "GND"; break; 
        default: prefix = "U"; break;
    }
    
    if (prefix == "GND") return "GND"; // Ground doesn't need unique ID usually? Or GND1, GND2.. keep standard 0 node.
    
    int maxIdx = 0;
    // Scan existing
    for (auto* item : scene->items()) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->name.startsWith(prefix)) {
                QString numPart = c->name.mid(prefix.length());
                bool ok;
                int idx = numPart.toInt(&ok);
                if (ok) maxIdx = std::max(maxIdx, idx);
            }
        }
    }
    return QString("%1%2").arg(prefix).arg(maxIdx + 1);
}

QString SchematicEditor::placeComponent(const QString& typeStr, QPointF pos, double rotation, const QJsonObject& params) {
    ComponentType type = ComponentType::Select;
    if (typeStr == "Resistor") type = ComponentType::Resistor;
    else if (typeStr == "Capacitor") type = ComponentType::Capacitor;
    else if (typeStr == "Inductor") type = ComponentType::Inductor;
    else if (typeStr == "VoltageSource" || typeStr == "Source") type = ComponentType::VoltageSource;
    else if (typeStr == "Ground" || typeStr == "GND") type = ComponentType::Ground;
    else if (typeStr == "Diode") type = ComponentType::Diode;
    else if (typeStr == "Mosfet" || typeStr == "NMOS" || typeStr == "PMOS") type = ComponentType::Mosfet;
    else if (typeStr == "Bjt" || typeStr == "NPN" || typeStr == "PNP") type = ComponentType::Bjt;
    
    if (type == ComponentType::Select) return "";
    
    VisualComponent* comp = new VisualComponent(type);
    
    // Auto-ID
    comp->name = generateUniqueId(type);
    
    // Apply Params
    if (params.contains("value")) comp->value = params["value"].toString();
    
    // Generic Params Map
    for(auto it = params.begin(); it != params.end(); ++it) {
        if (it.key() != "value") {
             comp->params[it.key()] = it.value().toString();
        }
    }
    
    // Position
    comp->setPos(pos);
    comp->setRotation(rotation);
    
    scene->addItem(comp);
    
    return comp->name;
}

bool SchematicEditor::moveComponent(const QString& id, double x, double y) {
    VisualComponent* comp = findComponent(id);
    if (!comp) return false;
    comp->setPos(x, y);
    return true;
}

bool SchematicEditor::rotateComponent(const QString& id, double degrees) {
    VisualComponent* comp = findComponent(id);
    if (!comp) return false;
    comp->setRotation(degrees);
    return true;
}

bool SchematicEditor::deleteComponent(const QString& id) {
    VisualComponent* comp = findComponent(id);
    if (!comp) return false;
    scene->removeItem(comp);
    delete comp;
    return true;
}

QRectF SchematicEditor::getSceneBounds() {
    if (!scene) return QRectF(0, 0, 100, 100);
    
    QRectF bounds;
    for (auto* item : scene->items()) {
        if (dynamic_cast<VisualComponent*>(item) || dynamic_cast<VisualWire*>(item)) {
            if (bounds.isNull()) {
                bounds = item->sceneBoundingRect();
            } else {
                bounds = bounds.united(item->sceneBoundingRect());
            }
        }
    }
    
    if (bounds.isNull()) {
        return QRectF(0, 0, 100, 100); // Default if empty
    }
    return bounds;
}

bool SchematicEditor::placeComponent(const QString& type, int x, int y, const QString& id, const QString& params) {
    // Convert to JSON params if provided
    QJsonObject jsonParams;
    if (!params.isEmpty()) {
        // Try to parse as JSON first
        QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
        if (doc.isObject()) {
            jsonParams = doc.object();
        } else {
            // Treat as simple value string
            jsonParams["value"] = params;
        }
    }
    
    QPointF pos(x, y);
    QString resultId = placeComponent(type, pos, 0.0, jsonParams);
    
    // If a specific ID was requested, rename the component
    if (!id.isEmpty() && !resultId.isEmpty()) {
        VisualComponent* comp = findComponent(resultId);
        if (comp) {
            comp->name = id;
        }
    }
    
    return !resultId.isEmpty();
}

