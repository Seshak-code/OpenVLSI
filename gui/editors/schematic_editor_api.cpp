#include "schematic_editor.h"
#include <QDebug>
#include <QJsonObject>

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
    ComponentType type = stringToComponentType(typeStr);
    
    if (type == ComponentType::Select || type == ComponentType::Wire) {
        qDebug() << "placeComponent: Unknown type:" << typeStr;
        return "";
    }
    
    VisualComponent* comp = new VisualComponent(type);
    
    // Auto-ID
    comp->name = generateUniqueId(type);
    
    // Apply Params
    if (params.contains("value")) comp->value = params["value"].toString();
    
    // Generic Params Map
    for(auto it = params.begin(); it != params.end(); ++it) {
        if (it.key() != "value" && it.key() != "rotation" && it.key() != "flipped") {
             comp->params[it.key()] = it.value().toString();
        }
    }
    
    // Position
    comp->setPos(pos);
    comp->setRotation(rotation);
    
    // Apply Flip
    if (params.contains("flipped")) {
        // Can be boolean or string "true"
        QJsonValue v = params["flipped"];
        if (v.isBool() && v.toBool()) comp->flip();
        else if (v.isString() && v.toString() == "true") comp->flip();
    }
    
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
    // 1. Collision check (Obstruction Awareness)
    // Assume standard component size 40x40 for check if not specified
    if (collidesWithAny(x - 20, y - 20, 40, 40)) {
        qDebug() << "AI Placement Rejected: Collision detected at (" << x << "," << y << ")";
        // In a real implementation, we would emit a signal captured by the AI Assistant to show a warning
        return false; 
    }

    // 2. Convert to JSON params if provided
    QJsonObject jsonParams;
    double rotation = 0.0;
    
    if (!params.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
        if (doc.isObject()) {
            jsonParams = doc.object();
            if (jsonParams.contains("rotation")) {
                rotation = jsonParams["rotation"].toDouble();
            }
        } else {
            jsonParams["value"] = params;
        }
    }
    
    QPointF pos(x, y);
    QString resultId = placeComponent(type, pos, rotation, jsonParams);
    
    if (!id.isEmpty() && !resultId.isEmpty()) {
        VisualComponent* comp = findComponent(resultId);
        if (comp) comp->name = id;
    }
    
    return !resultId.isEmpty();
}

QString SchematicEditor::placeProbe(const QString& id, QPointF bodyPos, QPointF posTip, QPointF negTip) {
    if (!scene) return "";

    VisualComponent* comp = new VisualComponent(ComponentType::Probe);
    comp->name = id.isEmpty() ? generateUniqueId(ComponentType::Probe) : id;
    comp->setPos(bodyPos);
    
    scene->addItem(comp);
    
    // Set tips relative to body
    if (comp->probePositiveTip) {
        comp->probePositiveTip->setPos(comp->mapFromScene(posTip));
    }
    if (comp->probeNegativeTip) {
        comp->probeNegativeTip->setPos(comp->mapFromScene(negTip));
    }
    
    return comp->name;
}

bool SchematicEditor::collidesWithAny(int x, int y, int w, int h, const QString& excludeId) {
    if (!scene) return false;
    
    VisualComponent* excludeComp = excludeId.isEmpty() ? nullptr : findComponent(excludeId);
    return scene->isOccupied(QRectF(x, y, w, h), excludeComp);
}

// ============================================================================
// PHASE 1.3: RELATIONAL POSITIONING API
// ============================================================================

bool SchematicEditor::placeNextTo(const QString& type, const QString& referenceId, 
                                   RelativeDirection dir, int margin, const QString& params) {
    VisualComponent* ref = findComponent(referenceId);
    if (!ref) {
        qDebug() << "placeNextTo: Reference component not found:" << referenceId;
        return false;
    }
    
    QRectF refBounds = ref->sceneBoundingRect();
    int x = 0, y = 0;
    
    switch (dir) {
        case RelativeDirection::Right:
            x = static_cast<int>(refBounds.right()) + margin;
            y = static_cast<int>(refBounds.center().y());
            break;
        case RelativeDirection::Left:
            x = static_cast<int>(refBounds.left()) - margin;
            y = static_cast<int>(refBounds.center().y());
            break;
        case RelativeDirection::Below:
            x = static_cast<int>(refBounds.center().x());
            y = static_cast<int>(refBounds.bottom()) + margin;
            break;
        case RelativeDirection::Above:
            x = static_cast<int>(refBounds.center().x());
            y = static_cast<int>(refBounds.top()) - margin;
            break;
    }
    
    // Snap to grid
    x = (x / 20) * 20;
    y = (y / 20) * 20;
    
    return placeComponent(type, x, y, "", params);
}

bool SchematicEditor::alignWith(const QString& targetId, const QString& referenceId, bool horizontal) {
    VisualComponent* target = findComponent(targetId);
    VisualComponent* ref = findComponent(referenceId);
    
    if (!target || !ref) {
        qDebug() << "alignWith: Component not found";
        return false;
    }
    
    QPointF refCenter = ref->sceneBoundingRect().center();
    QPointF targetPos = target->pos();
    
    if (horizontal) {
        // Align horizontal centers (same Y)
        targetPos.setY(refCenter.y());
    } else {
        // Align vertical centers (same X)
        targetPos.setX(refCenter.x());
    }
    
    // Snap to grid
    targetPos.setX(std::round(targetPos.x() / 20) * 20);
    targetPos.setY(std::round(targetPos.y() / 20) * 20);
    
    target->setPos(targetPos);
    return true;
}

bool SchematicEditor::optimizeComponentRotation(const QString& id) {
    VisualComponent* comp = findComponent(id);
    if (!comp) return false;
    
    // HEURISTIC:
    // 1. Identify "Portness" of neighbors.
    // 2. If neighbor is to the Left, we want our Input (if any) to face Left.
    // 3. If neighbor is Right, Output face Right.
    // 4. If PowerVdd is above, VDD pin up.
    
    // Simplified Heuristic: Minimize distance of terminals to nearest other component terminals.
    
    double bestScore = 1e9;
    double bestRot = 0;
    double currentRot = comp->rotation();
    
    // Collect interesting points (neighbors)
    QVector<QPointF> neighborPoints;
    QRectF myBounds = comp->sceneBoundingRect();
    double searchRadius = 300.0;
    
    for (auto* item : scene->items()) {
        if (auto* other = dynamic_cast<VisualComponent*>(item)) {
            if (other == comp) continue;
            double d = QLineF(myBounds.center(), other->sceneBoundingRect().center()).length();
            if (d < searchRadius) {
                // Add its terminals
                QVector<QPointF> terms = other->getTerminals();
                for(auto t : terms) neighborPoints.append(other->mapToScene(t));
            }
        }
    }
    
    if (neighborPoints.isEmpty()) return false; // No context
    
    // Try 0, 90, 180, 270
    for (int r = 0; r < 360; r += 90) {
        comp->setRotation(r);
        double score = 0;
        
        QVector<QPointF> myTerms = comp->getTerminals();
        
        for (const QPointF& myT_local : myTerms) {
            QPointF myT_scene = comp->mapToScene(myT_local);
            
            // Find closest neighbor terminal
            double minD = 1e9;
            for (const QPointF& nb : neighborPoints) {
                double d = QLineF(myT_scene, nb).length();
                if (d < minD) minD = d;
            }
            score += minD;
        }
        
        // Bonus for "Standard" orientation (0) if scores are close
        if (r == 0) score -= 10; 
        
        if (score < bestScore) {
            bestScore = score;
            bestRot = r;
        }
    }
    
    comp->setRotation(bestRot);
    return (bestRot != currentRot);
}

QPoint SchematicEditor::findClearSpace(int width, int height) {
    if (!scene) return QPoint(100, 100);
    
    QRectF bounds = getSceneBounds();
    
    // Strategy: Scan right edge of existing circuit, then scan downward
    int startX = static_cast<int>(bounds.right()) + 60;
    int startY = static_cast<int>(bounds.top());
    
    // Grid-align
    startX = ((startX + 19) / 20) * 20;
    startY = (startY / 20) * 20;
    
    // Scan for clear space in a grid pattern
    for (int y = startY; y < startY + 500; y += 20) {
        for (int x = startX; x < startX + 500; x += 20) {
            if (!collidesWithAny(x - width/2, y - height/2, width, height)) {
                return QPoint(x, y);
            }
        }
    }
    
    // Fallback: far right
    return QPoint(startX + 100, startY);
}

QString SchematicEditor::getSpatialContext() {
    if (!scene) return "No scene available.";
    
    QString context;
    QList<VisualComponent*> components;
    QList<VisualWire*> wires;
    
    // Collect all components and wires
    for (auto* item : scene->items()) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            components.append(c);
        } else if (auto* w = dynamic_cast<VisualWire*>(item)) {
            wires.append(w);
        }
    }
    
    if (components.isEmpty() && wires.isEmpty()) {
        return "The canvas is empty.";
    }
    
    // Sort components by X position (left to right)
    std::sort(components.begin(), components.end(), [](VisualComponent* a, VisualComponent* b) {
        return a->pos().x() < b->pos().x();
    });
    
    context = QString("COMPONENTS: %1 | WIRES: %2\n\n").arg(components.size()).arg(wires.size());
    
    // ========== COMPONENT SECTION ==========
    context += "COMPONENT LAYOUT:\n";
    for (int i = 0; i < components.size(); ++i) {
        VisualComponent* c = components[i];
        QString desc = QString("  %1 (%2) at (%3, %4)")
            .arg(c->name)
            .arg(componentTypeToString(c->type))
            .arg(static_cast<int>(c->pos().x()))
            .arg(static_cast<int>(c->pos().y()));
        
        // Find neighbors
        QStringList neighbors;
        for (int j = 0; j < components.size(); ++j) {
            if (i == j) continue;
            VisualComponent* other = components[j];
            
            double dx = other->pos().x() - c->pos().x();
            double dy = other->pos().y() - c->pos().y();
            double dist = std::sqrt(dx*dx + dy*dy);
            
            if (dist < 150) {
                QString rel;
                if (std::abs(dx) > std::abs(dy)) {
                    rel = (dx > 0) ? "→" : "←";
                } else {
                    rel = (dy > 0) ? "↓" : "↑";
                }
                neighbors << QString("%1%2").arg(rel).arg(other->name);
            }
        }
        
        if (!neighbors.isEmpty()) {
            desc += " [" + neighbors.join(" ") + "]";
        }
        
        context += desc + "\n";
    }
    
    // ========== WIRE/CONNECTIVITY SECTION ==========
    if (!wires.isEmpty()) {
        context += "\nCONNECTIVITY:\n";
        
        // Group wires by net name
        QMap<QString, QList<VisualWire*>> netGroups;
        for (auto* w : wires) {
            QString netName = w->getName().isEmpty() ? "Unnamed" : w->getName();
            netGroups[netName].append(w);
        }
        
        for (auto it = netGroups.begin(); it != netGroups.end(); ++it) {
            QString netName = it.key();
            QList<VisualWire*>& netWires = it.value();
            
            // Find components connected to this net
            QStringList connectedComponents;
            for (auto* w : netWires) {
                QLineF line = w->line();
                QPointF p1 = w->mapToScene(line.p1());
                QPointF p2 = w->mapToScene(line.p2());
                
                for (auto* c : components) {
                    QVector<QPointF> terminals = c->getTerminals();
                    for (const QPointF& term : terminals) {
                        QPointF sceneTerm = c->mapToScene(term);
                        double d1 = QLineF(sceneTerm, p1).length();
                        double d2 = QLineF(sceneTerm, p2).length();
                        
                        if (d1 < 15 || d2 < 15) { // Within snap distance
                            if (!connectedComponents.contains(c->name)) {
                                connectedComponents.append(c->name);
                            }
                        }
                    }
                }
            }
            
            if (connectedComponents.size() >= 2) {
                context += QString("  Net '%1': %2\n")
                    .arg(netName)
                    .arg(connectedComponents.join(" ↔ "));
            } else if (connectedComponents.size() == 1) {
                context += QString("  Net '%1': %2 (open-ended)\n")
                    .arg(netName)
                    .arg(connectedComponents.first());
            }
        }
    }
    
    // ========== FLOATING TERMINALS SECTION ==========
    context += "\nTERMINAL STATUS:\n";
    int floatingCount = 0;
    for (auto* c : components) {
        QVector<QPointF> terminals = c->getTerminals();
        QStringList floatingTerms;
        
        for (int t = 0; t < terminals.size(); ++t) {
            QPointF sceneTerm = c->mapToScene(terminals[t]);
            bool connected = false;
            
            for (auto* w : wires) {
                QLineF line = w->line();
                QPointF p1 = w->mapToScene(line.p1());
                QPointF p2 = w->mapToScene(line.p2());
                
                double d1 = QLineF(sceneTerm, p1).length();
                double d2 = QLineF(sceneTerm, p2).length();
                
                if (d1 < 15 || d2 < 15) {
                    connected = true;
                    break;
                }
            }
            
            if (!connected) {
                floatingTerms << QString("T%1").arg(t + 1);
                floatingCount++;
            }
        }
        
        if (!floatingTerms.isEmpty()) {
            context += QString("  ⚠ %1: %2 floating\n")
                .arg(c->name)
                .arg(floatingTerms.join(", "));
        }
    }
    
    if (floatingCount == 0 && !components.isEmpty()) {
        context += "  ✓ All terminals connected.\n";
    }
    
    // ========== CLEAR SPACE HINT ==========
    QPoint clearSpace = findClearSpace(60, 60);
    context += QString("\nNEXT CLEAR SPACE: (%1, %2)\n").arg(clearSpace.x()).arg(clearSpace.y());
    
    return context;
}

QPointF SchematicEditor::findNodeCenter(const QString& nodeName) {
    if (!scene) return QPointF(0, 0);
    
    QPointF center(0, 0);
    int count = 0;
    
    // Scan wires with matching net name
    for (auto* item : scene->items()) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (w->getName() == nodeName) {
                center += w->line().center();
                count++;
            }
        }
    }
    
    // Also check component terminals connected to this node (via connectivity map)
    // This is expensive to compute, so relying on named wires is faster.
    // If no named wires found, return 0,0
    
    if (count > 0) {
        return center / count;
    }
    
    // Fallback: If nodeName is "0" or "GND", find Ground components
    if (nodeName == "0" || nodeName == "GND") {
        for (auto* item : scene->items()) {
             if (auto* c = dynamic_cast<VisualComponent*>(item)) {
                 if (c->type == ComponentType::Ground) {
                     return c->pos() + QPointF(0, -20); // Terminal pos
                 }
             }
        }
    }

    return QPointF(0, 0);
}

QVector<QPointF> SchematicEditor::getComponentTerminals(const QString& id) {
    if (!scene) return {};
    
    VisualComponent* c = findComponent(id);
    if (!c) return {};
    
    QVector<QPointF> sceneTerminals;
    QVector<QPointF> localTerminals = c->getTerminals();
    
    for (const QPointF& pt : localTerminals) {
        sceneTerminals.append(c->mapToScene(pt));
    }
    return sceneTerminals;
}

