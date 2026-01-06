#include "schematic_editor.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <cmath>
#include <QDebug>

// ============================================================================
// WIRE COMPONENT
// ============================================================================
// Implementations moved to visual_component.cpp


// ============================================================================
// GRID SCENE IMPLEMENTATION
// ============================================================================
GridScene::GridScene(QObject* parent) : QGraphicsScene(parent) {
    setSceneRect(-5000, -5000, 10000, 10000);
    
    connect(this, &QGraphicsScene::selectionChanged, this, [this](){
        auto items = selectedItems();
        if (items.isEmpty()) {
            emit componentSelected(nullptr); // Clears everything
        } else {
            // Priority selection
            QGraphicsItem* item = items.first();
            if (auto* sc = dynamic_cast<VisualComponent*>(item)) {
                emit componentSelected(sc);
            } else if (auto* w = dynamic_cast<VisualWire*>(item)) {
                emit wireSelected(w);
            } else {
                emit componentSelected(nullptr);
            }
        }
    });
}


void GridScene::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor("#1e1e1e")); 

    if (!gridVisible) return;

    const int gridSize = GRID_STEP;
    int left = static_cast<int>(std::floor(rect.left()));
    int top = static_cast<int>(std::floor(rect.top()));
    int right = static_cast<int>(std::ceil(rect.right()));
    int bottom = static_cast<int>(std::ceil(rect.bottom()));

    left -= (left % gridSize);
    top -= (top % gridSize);

    QPen gridPen(QColor(60, 60, 60));
    gridPen.setCosmetic(true);
    painter->setPen(gridPen);

    for (int x = left; x <= right; x += gridSize) {
        painter->drawLine(x, top, x, bottom);
    }
    for (int y = top; y <= bottom; y += gridSize) {
        painter->drawLine(left, y, right, y);
    }
    
    painter->setPen(QPen(Qt::white, 2));
    painter->drawLine(0, top, 0, bottom);
    painter->drawLine(left, 0, right, 0);
}

QPointF GridScene::snapToGrid(const QPointF& pos) {
    if (!snappingEnabled) return pos;
    double x = std::round(pos.x() / GRID_STEP) * GRID_STEP;
    double y = std::round(pos.y() / GRID_STEP) * GRID_STEP;
    return QPointF(x, y);
}

void GridScene::clearGhostWires() {
    for (auto* wire : ghostWires) {
        if (wire && wire->scene()) {
            removeItem(wire);
        }
        delete wire;
    }
    ghostWires.clear();
}

void GridScene::setTool(ComponentType t) {
    currentTool = t;
    if (ghostItem) {
        removeItem(ghostItem);
        delete ghostItem;
        ghostItem = nullptr;
    }
    
    // Cleanup any partial wiring
    isWiring = false;
    clearGhostWires();
    
    if (t != ComponentType::Select && t != ComponentType::Wire) {
        ghostItem = new VisualComponent(t);
        ghostItem->setOpacity(0.5);
        // Ensure ghost ignores collisions so checking code works on other stuff
        ghostItem->setZValue(100); 
        addItem(ghostItem);
    }
    
    emit toolChanged(t);
}

void GridScene::setGridVisible(bool visible) {
    gridVisible = visible;
    update();
}

void GridScene::setSnappingEnabled(bool enabled) {
    snappingEnabled = enabled;
}

bool GridScene::isWireValid(QGraphicsLineItem* wire) {
    if (!wire) return false;
    // Check collision with all items that are VisualComponents
    QList<QGraphicsItem*> collisions = wire->collidingItems();
    for (auto* item : collisions) {
        if (item == wire || item == ghostItem) continue;
        if (dynamic_cast<VisualComponent*>(item)) {
            // Found a component body collision
            return false; 
        }
    }
    return true;
}

bool GridScene::isPlacementValid(const QPointF& pos, QGraphicsItem* item) {
    QPainterPath shape;
    if (item) {
        shape = item->shape();
        shape.translate(pos); 
    } else {
        shape.addRect(pos.x()-2, pos.y()-2, 4, 4); 
    }

    auto collisions = items(shape);
    for (auto* col : collisions) {
        if (col == item || col == ghostItem || ghostWires.contains(static_cast<VisualWire*>(col))) continue;
        if (col->type() == item->type()) return false; 
    }
    return true;
}

void GridScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF p = snapToGrid(event->scenePos());
        
        if (currentTool == ComponentType::Select) {
            QGraphicsScene::mousePressEvent(event);
        }
        else if (currentTool == ComponentType::Wire) {
            if (!isWiring) {
                // START WIRING
                isWiring = true;
                wireStart = p;
                clearGhostWires();
                emit messageLogged("Started wiring.");
            } else {
                // COMMIT SEGMENTS (Click)
                // We convert current ghosts to permanent wires
                // Update wireStart to current p
                
                // Validate ghosts first
                bool allValid = true;
                for(auto* w : ghostWires) {
                    if (w->line().length() > 0 && !isWireValid(w)) {
                        allValid = false; 
                        break;
                    }
                }
                
                if (allValid) {
                     for(auto* w : ghostWires) {
                         if (w->isVisible() && w->line().length() > 0) {
                             // Create permanent copy
                             VisualWire* perm = new VisualWire(w->line());
                             addItem(perm);
                         }
                     }
                     wireStart = p;
                     // Clean to prep next segment
                     clearGhostWires();
                     emit messageLogged("Segment placed.");
                     
                     // Check if we hit a terminal? (Simplified: Just check if we hit a component pin)
                     // If so, finish.
                     // For now, let user double click or ESC to finish.
                } else {
                    emit messageLogged("Cannot place wire here!");
                }
            }
        }
        else {
            // Placement
            QRectF predictedBounds = ghostItem->boundingRect();
            predictedBounds.translate(p.x(), p.y());
            
            bool colliding = false;
            auto itemsAt = items(predictedBounds, Qt::IntersectsItemBoundingRect);
            for(auto* i : itemsAt) {
                 if (i != ghostItem) { 
                     // Ignore wires during component placement? Usually yes.
                     // But for now, strict.
                     if (dynamic_cast<VisualComponent*>(i)) { colliding = true; break; }
                 }
            }

            if (!colliding) {
                auto* item = new VisualComponent(currentTool);
                item->setPos(p);
                item->setRotation(ghostItem->rotation());
                addItem(item);
                emit messageLogged("Placed component.");
            } else {
                emit messageLogged("Invalid Placement: Overlap!");
            }
        }
    } else if (event->button() == Qt::RightButton) {
        if (isWiring) {
            // Cancel/Finish wiring
            isWiring = false;
            clearGhostWires();
            emit messageLogged("Wiring finished.");
        } else {
            setTool(ComponentType::Select);
        }
    }
}

void GridScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    // No-op for wiring now (click-click interaction)
    QGraphicsScene::mouseReleaseEvent(event);
}

void GridScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QPointF p = snapToGrid(event->scenePos());
    emit mouseMoved((int)p.x(), (int)p.y());

    if (currentTool == ComponentType::Select) {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

    if (currentTool == ComponentType::Wire && isWiring) {
        // Orthogonal L-Shape Routing
        // Path: wireStart -> Corner -> p
        // Corner is (p.x, wireStart.y) -> Horizontal first
        
        QPointF corner(p.x(), wireStart.y());
        
        // We reuse or create ghost wires as needed
        // Case 1: Straight horizontal (y aligned) or vertical (x aligned)
        // Case 2: L-shape
        
        if (ghostWires.isEmpty()) {
            ghostWires.append(new VisualWire(QLineF(wireStart, wireStart))); // 1
            ghostWires.append(new VisualWire(QLineF(wireStart, wireStart))); // 2
            addItem(ghostWires[0]);
            addItem(ghostWires[1]);
        }
        
        // Update Segments
        ghostWires[0]->setLine(QLineF(wireStart, corner));
        ghostWires[1]->setLine(QLineF(corner, p));
        
        // Validity Check (Red/Cyan)
        for(auto* w : ghostWires) {
            if (w->line().length() > 0) {
                 w->setVisible(true);
                 if (isWireValid(w)) w->setPen(QPen(Qt::cyan, 2));
                 else w->setPen(QPen(Qt::red, 2));
            } else {
                 w->setVisible(false); // Hide zero length segments
            }
        }
    }
    
    if (ghostItem) {
        ghostItem->setPos(p);
    }
}



void GridScene::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        auto selection = selectedItems();
        for (auto* item : selection) {
            removeItem(item);
            delete item;
        }
    }
    else if (event->key() == Qt::Key_Escape) {
        setTool(ComponentType::Select);
    }
    else if (event->key() == Qt::Key_R || event->key() == Qt::Key_Space) {
        if (ghostItem) {
            ghostItem->rotate90();
        } else {
            for (auto* item : selectedItems()) {
                if (auto* sc = dynamic_cast<VisualComponent*>(item)) {
                    sc->rotate90();
                }
            }
        }
    }
    else {
        QGraphicsScene::keyPressEvent(event);
    }
}

// ============================================================================
// WIRE PLACEMENT API (for AI automation)
// ============================================================================
void GridScene::placeWirePath(const QVector<QPointF>& points) {
    if (points.size() < 2) return;
    
    for (int i = 0; i < points.size() - 1; ++i) {
        QPointF p1 = snapToGrid(points[i]);
        QPointF p2 = snapToGrid(points[i + 1]);
        
        VisualWire* wire = new VisualWire(QLineF(p1, p2));
        addItem(wire);
    }
}

// ============================================================================
// NETLIST GENERATION
// ============================================================================

// Helper: Parse Value Strings (e.g. 10k, 1.2u)
static double parseValue(const QString& str) {
    if (str.isEmpty()) return 0.0;
    
    // Extract numeric part
    QString numStr;
    QString suffix;
    bool suffixFound = false;
    
    for (QChar c : str) {
        if (!suffixFound && (c.isDigit() || c == '.' || c == '-')) {
            numStr.append(c);
        } else {
            suffixFound = true;
            suffix.append(c);
        }
    }
    
    double val = numStr.toDouble();
    suffix = suffix.trimmed();
    
    if (suffix.startsWith("k") || suffix.startsWith("K")) val *= 1e3;
    else if (suffix.startsWith("M") && suffix.length() == 1) val *= 1e6; // Mega
    else if (suffix.startsWith("Meg")) val *= 1e6;
    else if (suffix.startsWith("G")) val *= 1e9;
    else if (suffix.startsWith("m")) val *= 1e-3;
    else if (suffix.startsWith("u")) val *= 1e-6;
    else if (suffix.startsWith("n")) val *= 1e-9;
    else if (suffix.startsWith("p")) val *= 1e-12;
    else if (suffix.startsWith("f")) val *= 1e-15;
    
    return val;
}

TensorNetlist GridScene::generateNetlist() {
    TensorNetlist netlist;
    
    // --- Step 1: Connectivity Analysis (Spatial Approach) ---
    QMap<QPair<int,int>, int> pointToNetID;
    int nextNetID = 1; 
    
    // Function to get distinct net ID for a point, creating new if needed
    auto getNetID = [&](QPointF p) -> int {
        QPair<int,int> key = {(int)round(p.x()), (int)round(p.y())};
        if (!pointToNetID.contains(key)) {
            pointToNetID[key] = nextNetID++;
        }
        return pointToNetID[key];
    };
    
    // Temporary structure to merge sets: map old_id -> new_id
    QMap<int, int> merges;
    
    // Find representative set
    std::function<int(int)> findSet = [&](int id) {
        while (merges.contains(id)) {
            id = merges[id];
        }
        return id;
    };
    
    auto unionSets = [&](int id1, int id2) {
        int root1 = findSet(id1);
        int root2 = findSet(id2);
        if (root1 != root2) {
            // Merge higher into lower
            if (root1 < root2) merges[root2] = root1;
            else merges[root1] = root2;
        }
    };
    
    // 1.1 Traverse Wires -> Unify points along the wire
    QList<QGraphicsItem*> allItems = items();
    for (auto* item : allItems) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (!w->isVisible()) continue;
            
            QLineF line = w->line();
            // Normalize direction for iteration
            int x1 = (int)round(line.x1());
            int y1 = (int)round(line.y1());
            int x2 = (int)round(line.x2());
            int y2 = (int)round(line.y2());
            
            // Unify Start
            int startID = getNetID(QPointF(x1, y1));
            
            // Walk the line
            if (x1 == x2) { // Vertical
                int minY = std::min(y1, y2);
                int maxY = std::max(y1, y2);
                for (int y = minY; y <= maxY; y += GRID_STEP) {
                    unionSets(startID, getNetID(QPointF(x1, y)));
                }
            } else if (y1 == y2) { // Horizontal
                int minX = std::min(x1, x2);
                int maxX = std::max(x1, x2);
                for (int x = minX; x <= maxX; x += GRID_STEP) {
                    unionSets(startID, getNetID(QPointF(x, y1)));
                }
            } else {
                // Diagonal (Shouldn't happen with current tool but handle endpoints)
                unionSets(startID, getNetID(QPointF(x2, y2)));
            }
        }
    }
    
    // 1.2 Traverse Components -> Get Terminals -> Get Net IDs
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            QVector<QPointF> terms = c->getTerminals();
            for (const auto& localP : terms) {
                // Ensure this point gets an ID
                getNetID(c->mapToScene(localP));
            }
        }
    }
    
    // 1.3 OPTIONAL: Unify Co-located Component Pins?
    // Already implicit because getNetID handles spatial hashing. 
    // If two components have pins at (100,100), getNetID returns same ID for both.
    
    // Step 2: Resolve Global Nodes
    QMap<int, int> finalNodeMap;
    int currentNodeIndex = 1; // 0 is reserved for Ground
    
    auto getFinalNode = [&](QPointF p) -> int {
        int rawID = getNetID(p);
        int rootID = findSet(rawID);
        if (!finalNodeMap.contains(rootID)) {
            finalNodeMap[rootID] = currentNodeIndex++;
        }
        return finalNodeMap[rootID];
    };
    
    // Check for Ground Components to fix node 0
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Ground) {
                QPointF termPos = c->mapToScene(c->getTerminals()[0]);
                int rawID = getNetID(termPos);
                int rootID = findSet(rawID);
                finalNodeMap[rootID] = 0; 
            }
        }
    }
    
    // Step 3: Populate Netlist & Enforce Counts
    for (auto* item : allItems) {
        auto* c = dynamic_cast<VisualComponent*>(item);
        if (!c) continue;
        
        QVector<int> nodes;
        for (QPointF p : c->getTerminals()) {
            nodes.append(getFinalNode(c->mapToScene(p)));
        }
        
        double val = parseValue(c->value); 
        
        // Safety Checks
        int expected = 0;
        switch(c->type) {
            case ComponentType::Resistor: expected = 2; break;
            case ComponentType::Capacitor: expected = 2; break;
            case ComponentType::Inductor: expected = 2; break;
            case ComponentType::VoltageSource: expected = 2; break;
            case ComponentType::CurrentSource: expected = 2; break;
            case ComponentType::Diode: expected = 2; break;
            case ComponentType::SchottkyDiode: expected = 2; break;
            case ComponentType::Mosfet: expected = 3; break; // G, D, S
            case ComponentType::Bjt: expected = 3; break; // C, B, E
            default: break;
        }
        
        // BJT VisualComponent check in visual_component.cpp might need update if it only returns 2 points.
        // Assuming it's correct or we leniently skip if mismatches.
        if (c->type != ComponentType::Ground && nodes.size() < expected) {
            emit messageLogged(QString("Warning: Component %1 has incomplete terminals!").arg(c->name));
            continue;
        }
        
        switch(c->type) {
            case ComponentType::Resistor:
                netlist.addResistor(nodes[0], nodes[1], val);
                break;
            case ComponentType::Capacitor:
                netlist.addCapacitor(nodes[0], nodes[1], val);
                break;
            case ComponentType::Inductor:
                netlist.addInductor(nodes[0], nodes[1], val);
                break;
            case ComponentType::VoltageSource:
                netlist.addVoltageSource(nodes[0], nodes[1], val);
                break;
            case ComponentType::CurrentSource:
                netlist.addCurrentSource(nodes[0], nodes[1], val);
                break;
            case ComponentType::Ground:
                break;
            case ComponentType::Diode:
                netlist.addDiode(nodes[0], nodes[1], 1e-14); 
                break;
            case ComponentType::SchottkyDiode:
                netlist.addDiode(nodes[0], nodes[1], 200e-9); // Higher Is for lower Vf (~0.3V)
                break;
            case ComponentType::Mosfet:
                {
                   double W = parseValue(c->params.value("W", "1u"));
                   double L = parseValue(c->params.value("L", "180n"));
                   std::string model = c->params.value("Model", "nfet").toStdString();
                   // Order: G(0), D(1), S(2)? Check VisualComponent
                   // Assuming D G S mapping for safety or standardized order
                   // visual_component says:  t << QPointF(-20, 0) << QPointF(20, 0) << QPointF(0, 20); // G D S
                   // Indices: 0=G, 1=D, 2=S
                   netlist.addMosfet(c->name.toStdString(), nodes[1], nodes[0], nodes[2], nodes[2], model, W, L); 
                }
                break;
            case ComponentType::Bjt:
                {
                    std::string model = c->params.value("Model", "2N2222").toStdString();
                    // Need to check BJT terminals in visual_component.cpp
                    // If it has 3, we map C B E.
                    if (nodes.size() >= 3)
                        netlist.addBJT(c->name.toStdString(), nodes[0], nodes[1], nodes[2], model, true);
                    else
                         emit messageLogged(QString("Warning: BJT %1 pins definition incomplete").arg(c->name));
                }
                break;
            default:
                break;
        }
    }
    
    return netlist; 
}

// ============================================================================
// ERC (Electrical Rule Check)
// ============================================================================
QStringList GridScene::runERC() {
    QStringList errors;
    
    // --- Build Connectivity (Duplicated logic from generateNetlist, could be refactored) ---
    QMap<QPair<int,int>, int> pointToNetID;
    int nextNetID = 1;
    
    auto getNetID = [&](QPointF p) -> int {
        QPair<int,int> key = {(int)round(p.x()), (int)round(p.y())};
        if (!pointToNetID.contains(key)) {
            pointToNetID[key] = nextNetID++;
        }
        return pointToNetID[key];
    };
    
    QMap<int, int> merges;
    std::function<int(int)> findSet = [&](int id) {
        while (merges.contains(id)) id = merges[id];
        return id;
    };
    
    auto unionSets = [&](int id1, int id2) {
        int r1 = findSet(id1), r2 = findSet(id2);
        if (r1 != r2) { if (r1 < r2) merges[r2] = r1; else merges[r1] = r2; }
    };
    
    // Wire Traversal
    QList<QGraphicsItem*> allItems = items();
    for (auto* item : allItems) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
            if (!w->isVisible()) continue;
            QLineF line = w->line();
            int x1 = (int)round(line.x1()), y1 = (int)round(line.y1());
            int x2 = (int)round(line.x2()), y2 = (int)round(line.y2());
            int startID = getNetID(QPointF(x1, y1));
            if (x1 == x2) { for (int y = std::min(y1,y2); y <= std::max(y1,y2); y += GRID_STEP) unionSets(startID, getNetID(QPointF(x1, y))); }
            else if (y1 == y2) { for (int x = std::min(x1,x2); x <= std::max(x1,x2); x += GRID_STEP) unionSets(startID, getNetID(QPointF(x, y1))); }
            else { unionSets(startID, getNetID(QPointF(x2, y2))); }
        }
    }
    
    // Component Terminals
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            for (const auto& localP : c->getTerminals()) getNetID(c->mapToScene(localP));
        }
    }
    
    // Count connections per net
    QMap<int, int> netConnectionCount; // Root ID -> Count
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            for (const auto& localP : c->getTerminals()) {
                int rawID = getNetID(c->mapToScene(localP));
                int rootID = findSet(rawID);
                netConnectionCount[rootID]++;
            }
        }
    }
    
    // --- ERC Checks ---
    
    // 1. Missing Ground
    bool hasGround = false;
    for (auto* item : allItems) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            if (c->type == ComponentType::Ground) { hasGround = true; break; }
        }
    }
    if (!hasGround) {
        errors << "ERC Error: No Ground component found!";
    }
    
    // 2. Floating Nodes (nets with only 1 connection)
    for (auto it = netConnectionCount.begin(); it != netConnectionCount.end(); ++it) {
        if (it.value() == 1) {
            errors << QString("ERC Warning: Net has only 1 connection (floating?)");
        }
    }
    
    // 3. Unconnected Components (Any component not touching a wire?)
    // Simple heuristic: If a pin's net has only 1 connection, it's from this pin alone.
    // Already covered by #2. More advanced: check if pin touches a wire endpoint.
    
    return errors;
}

// ============================================================================
// EDITOR WIDGET
// ============================================================================
SchematicEditor::SchematicEditor(QWidget* parent) : QWidget(parent) {
    scene = new GridScene(this);
    view = new QGraphicsView(scene, this);
    
    setupUi();
    
    connect(scene, &GridScene::mouseMoved, this, [this](int x, int y){
        statusLabel->setText(QString("X: %1, Y: %2").arg(x).arg(y));
    });
    connect(scene, &GridScene::messageLogged, this, [this](const QString& msg){
        statusLabel->setText(msg);
    });
    connect(scene, &GridScene::toolChanged, this, [this](ComponentType t){
        if(toolButtons.contains(t)) {
            toolButtons[t]->setChecked(true);
            for(auto type : toolButtons.keys()) {
                if(type != t) toolButtons[type]->setChecked(false);
            }
             if (t == ComponentType::Select)
                view->setDragMode(QGraphicsView::RubberBandDrag);
            else
                view->setDragMode(QGraphicsView::NoDrag);
        }
    });

    connect(scene, &GridScene::componentSelected, this, &SchematicEditor::itemSelected);
    connect(scene, &GridScene::wireSelected, this, &SchematicEditor::wireSelected);
}

QWidget* SchematicEditor::createRibbon() {
    QGroupBox* ribbon = new QGroupBox("Tools");
    ribbon->setStyleSheet("QGroupBox { border: 1px solid #444; margin-top: 5px; font-weight: bold; color: #BBB; } "
                          "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; }");
    
    QHBoxLayout* layout = new QHBoxLayout(ribbon);
    layout->setContentsMargins(5, 10, 5, 5);
    layout->setSpacing(5);

    toolButtons[ComponentType::Select] = createToolButton("Select (Esc)", ComponentType::Select);
    layout->addWidget(toolButtons[ComponentType::Select]);
    
    QFrame* line = new QFrame(); 
    line->setFrameShape(QFrame::VLine); line->setStyleSheet("color: #555;");
    layout->addWidget(line);

    toolButtons[ComponentType::Wire] = createToolButton("Wire", ComponentType::Wire);
    layout->addWidget(toolButtons[ComponentType::Wire]);
    
    toolButtons[ComponentType::Resistor] = createToolButton("Resistor", ComponentType::Resistor);
    layout->addWidget(toolButtons[ComponentType::Resistor]);
    
    toolButtons[ComponentType::Capacitor] = createToolButton("Capacitor", ComponentType::Capacitor);
    layout->addWidget(toolButtons[ComponentType::Capacitor]);

    toolButtons[ComponentType::VoltageSource] = createToolButton("Source", ComponentType::VoltageSource);
    layout->addWidget(toolButtons[ComponentType::VoltageSource]);
    
    toolButtons[ComponentType::Ground] = createToolButton("GND", ComponentType::Ground);
    layout->addWidget(toolButtons[ComponentType::Ground]);

    layout->addStretch();
    return ribbon;
}

QPushButton* SchematicEditor::createToolButton(const QString& text, ComponentType type) {
    QPushButton* btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setFixedHeight(35);
    btn->setStyleSheet(
        "QPushButton { background: #333; color: white; border: 1px solid #555; border-radius: 3px; padding: 5px; }"
        "QPushButton:checked { background: #007ACC; border: 1px solid #0099FF; }"
        "QPushButton:hover { background: #444; }"
    );
    connect(btn, &QPushButton::clicked, this, [this, type](){
        scene->setTool(type);
    });
    return btn;
}

void SchematicEditor::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);
    
    QWidget* topArea = new QWidget();
    topArea->setStyleSheet("background-color: #252526;");
    QVBoxLayout* topLayout = new QVBoxLayout(topArea);
    
    QHBoxLayout* settingsLayout = new QHBoxLayout();
    QCheckBox* gridCheck = new QCheckBox("Show Grid");
    gridCheck->setChecked(true);
    gridCheck->setStyleSheet("color: #DDD;");
    connect(gridCheck, &QCheckBox::toggled, this, &SchematicEditor::onGridToggled);
    
    QCheckBox* snapCheck = new QCheckBox("Snap to Grid");
    snapCheck->setChecked(true);
    snapCheck->setStyleSheet("color: #DDD;");
    connect(snapCheck, &QCheckBox::toggled, this, &SchematicEditor::onSnappingToggled);

    settingsLayout->addWidget(gridCheck);
    settingsLayout->addWidget(snapCheck);
    settingsLayout->addStretch();
    
    topLayout->addLayout(settingsLayout);
    topLayout->addWidget(createRibbon());
    mainLayout->addWidget(topArea);

    view->setRenderHint(QPainter::Antialiasing);
    view->setBackgroundBrush(Qt::NoBrush); 
    view->setFrameShape(QFrame::NoFrame);
    // FORCE FULL VIEWPORT UPDATE TO FIX VISUAL BUGS
    view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    mainLayout->addWidget(view);

    statusLabel = new QLabel("Ready");
    statusLabel->setStyleSheet("background: #007ACC; color: white; padding: 4px; font-weight: bold;");
    mainLayout->addWidget(statusLabel);
    
    scene->setTool(ComponentType::Select);
}

void SchematicEditor::onGridToggled(bool checked) {
    scene->setGridVisible(checked);
}

void SchematicEditor::onSnappingToggled(bool checked) {
    scene->setSnappingEnabled(checked);
}

void SchematicEditor::setTool(ComponentType t) {
    scene->setTool(t);
}

void SchematicEditor::setWireName(VisualWire* wire, const QString& name) {
    if(wire) {
        wire->setName(name);
    }
}

QStringList SchematicEditor::getAvailableComponents() {
    return {
        "Resistor", "Capacitor", "Inductor", 
        "Ground", "Voltage Source", 
        "Diode", "N-MOSFET", "NPN BJT"
    };
}

void SchematicEditor::placeWirePath(const QVector<QPointF>& points) {
    scene->placeWirePath(points);
}

QJsonObject SchematicEditor::getCircuitState() {
    return scene->getCircuitState();
}

QString SchematicEditor::dumpSceneState() {
    QJsonObject state = getCircuitState();
    
    // Dump Components
    QString dump;
    QJsonArray components = state["components"].toArray();
    for (const auto& val : components) {
        QJsonObject c = val.toObject();
        dump += QString("COMPONENT: %1 Type:%2 Pos:(%3,%4) Rot:%5 Value:%6\n")
                .arg(c["name"].toString())
                .arg(c["type"].toString())
                .arg(c["x"].toDouble()).arg(c["y"].toDouble())
                .arg(c["rotation"].toDouble())
                .arg(c["properties"].toObject()["value"].toString());
    }
    
    // Dump Wires
    QJsonArray wires = state["wires"].toArray();
    for (const auto& val : wires) {
        QJsonObject w = val.toObject();
        QJsonArray points = w["points"].toArray();
        if (points.size() >= 2) {
             QJsonObject p1 = points[0].toObject();
             QJsonObject p2 = points[points.size()-1].toObject();
             dump += QString("WIRE: Net:%1 P1:(%2,%3) P2:(%4,%5)\n")
                     .arg(w["netName"].toString())
                     .arg(p1["x"].toDouble()).arg(p1["y"].toDouble())
                     .arg(p2["x"].toDouble()).arg(p2["y"].toDouble());
        }
    }
    
    return dump;
}

// ============================================================================
// GRID SCENE JSON SERIALIZATION
// ============================================================================
QJsonObject GridScene::getCircuitState() {
    QJsonObject root;
    
    // 1. Components
    QJsonArray components;
    for (auto* item : items()) {
        if (auto* c = dynamic_cast<VisualComponent*>(item)) {
            QJsonObject compInfo;
            compInfo["id"] = c->name; // Use component Name as stable ID (e.g. R1)
            compInfo["name"] = c->name;
            compInfo["type"] = componentTypeToString(c->type); 
            compInfo["x"] = c->pos().x();
            compInfo["y"] = c->pos().y();
            compInfo["rotation"] = c->rotation();
            
            QJsonObject props;
            props["value"] = c->value;
            // Serialize all dynamic parameters (e.g. W, L, Model)
            for(auto it = c->params.begin(); it != c->params.end(); ++it) {
                props[it.key()] = it.value();
            }
            compInfo["properties"] = props;
            
            components.append(compInfo);
        }
    }
    root["components"] = components;
    
    // 2. Wires
    QJsonArray wires;
    for (auto* item : items()) {
        if (auto* w = dynamic_cast<VisualWire*>(item)) {
             QJsonObject wireInfo;
             wireInfo["netName"] = w->getName().isEmpty() ? "UNNAMED" : w->getName();
             
             QJsonArray points;
             QLineF line = w->line();
             
             QJsonObject p1, p2;
             p1["x"] = line.p1().x(); p1["y"] = line.p1().y();
             p2["x"] = line.p2().x(); p2["y"] = line.p2().y();
             
             points.append(p1);
             points.append(p2);
             wireInfo["points"] = points;
             
             wires.append(wireInfo);
        }
    }
    root["wires"] = wires;
    
    // 3. Metadata
    QJsonObject meta;
    meta["gridVisible"] = gridVisible;
    meta["snappingEnabled"] = snappingEnabled;
    root["metadata"] = meta;
    
    return root;
}

bool GridScene::isOccupied(const QRectF& rect, QGraphicsItem* exclude) {
    // Search for items in this region
    QList<QGraphicsItem*> itemsInRect = items(rect, Qt::IntersectsItemBoundingRect);
    
    // Debug logging for empty scene issues
    if (itemsInRect.size() > 0) {
        // qDebug() << "Checking collision at:" << rect << "Found" << itemsInRect.size() << "items.";
    }

    for (auto* item : itemsInRect) {
        // Exclude self and ghost item
        if (item == exclude) continue;
        if (item == ghostItem) {
             // qDebug() << "Ignoring ghostItem at" << item->pos();
             continue;
        }
        
        // Exclude ghost wires
        bool isGhostWire = false;
        for (auto* gw : ghostWires) {
            if (item == gw) { isGhostWire = true; break; }
        }
        if (isGhostWire) continue;
        
        // Only count components and wires as "obstructions"
        if (auto* vc = dynamic_cast<VisualComponent*>(item)) {
            qDebug() << "COLLISION: Component" << vc->name << "(" << componentTypeToString(vc->type) << ") at" << vc->pos()
                     << "Bounds:" << vc->sceneBoundingRect() << "vs Check:" << rect;
            return true;
        } 
        else if (auto* vw = dynamic_cast<VisualWire*>(item)) {
            qDebug() << "COLLISION: Wire" << vw->getName() << "Line:" << vw->line() << "vs Check:" << rect;
            return true;
        }
    }
    return false;
}

QStringList SchematicEditor::runERC() {
    return scene->runERC();
}


