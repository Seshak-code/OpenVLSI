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

TensorNetlist GridScene::generateNetlist() {
    return TensorNetlist(); 
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

void SchematicEditor::setWireName(VisualWire* wire, const QString& name) {
    if (wire) {
        wire->setName(name);
    }
}

QString SchematicEditor::dumpSceneState() {
    QString dump;
    auto items = scene->items();
    for(auto* item : items) {
        if(auto* wc = dynamic_cast<VisualComponent*>(item)) {
            dump += QString("COMPONENT: %1 Type:%2 Pos:(%3,%4) Rot:%5 Value:%6\n")
                    .arg(wc->name).arg((int)wc->type)
                    .arg(wc->pos().x()).arg(wc->pos().y())
                    .arg(wc->rotation()).arg(wc->value);
        }
        else if(auto* ww = dynamic_cast<VisualWire*>(item)) {
             dump += QString("WIRE: Net:%1 P1:(%2,%3) P2:(%4,%5)\n")
                     .arg(ww->getName().isEmpty() ? "UNNAMED" : ww->getName())
                     .arg(ww->line().p1().x()).arg(ww->line().p1().y())
                     .arg(ww->line().p2().x()).arg(ww->line().p2().y());
        }
    }
    return dump;
}
