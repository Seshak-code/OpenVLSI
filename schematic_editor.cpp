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
WireComponent::WireComponent(QLineF line) : QGraphicsLineItem(line) {
    setPen(QPen(Qt::cyan, 2));
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(1); // Wires below components (which will be Z=2)
}

void WireComponent::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    painter->setPen(pen());
    if (isSelected()) {
        painter->setPen(QPen(Qt::green, 2, Qt::DashLine));
    }
    painter->drawLine(line());
}

QVariant WireComponent::itemChange(GraphicsItemChange change, const QVariant &value) {
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
// SIMPLE COMPONENT IMPLEMENTATION
// ============================================================================
SimpleComponent::SimpleComponent(ComponentType t) : type(t) {
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setTransformOriginPoint(0, 0); 
    setZValue(2); // Components above wires
    
    // Default Properties
    switch(t) {
        case ComponentType::Resistor: name = "R?"; value = "1k"; break;
        case ComponentType::Capacitor: name = "C?"; value = "1uF"; break;
        case ComponentType::Inductor: name = "L?"; value = "1mH"; break;
        case ComponentType::VoltageSource: name = "V?"; value = "5V"; break;
        default: name = "U?"; value = "0"; break;
    }
}

QRectF SimpleComponent::boundingRect() const {
    return QRectF(-50, -50, 100, 100);
}

void SimpleComponent::rotate90() {
    setRotation(rotation() + 90);
}

QVector<QPointF> SimpleComponent::getTerminals() const {
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
        case ComponentType::Bjt: // BJT NPN: C, B, E? Simplified for now
            t << QPointF(0, -40) << QPointF(0, 40);
            break;
        case ComponentType::Ground:
            t << QPointF(0, -20); // Top connection
            break;
        case ComponentType::Mosfet:
             t << QPointF(-20, 0) << QPointF(20, 0) << QPointF(0, 20); // G D S
             break;
        default:
            break;
    }
    return t;
}

QPainterPath SimpleComponent::shape() const {
    QPainterPath path;
    // Define the "Solid Body" that wires cannot cross
    // Must be smaller than terminal reach so wires can get to terminals
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
            path.addRect(-10, -5, 20, 10); // Plate
            break;
        default:
            path.addRect(-15, -15, 30, 30);
            break;
    }
    return path;
}

QVariant SimpleComponent::itemChange(GraphicsItemChange change, const QVariant &value) {
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

void SimpleComponent::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setPen(QPen(Qt::white, 2));
    painter->setBrush(QColor("#1e1e1e")); 
    
    // Draw Components (Grid Aligned)
    switch(type) {
        case ComponentType::Resistor:
            painter->drawRect(-20, -6, 40, 12);
            painter->drawLine(-40, 0, -20, 0); // Reach to 40
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
            painter->drawLine(0, -20, 0, 0); // Reach to -20
            break;
        case ComponentType::VoltageSource:
            painter->drawEllipse(-20, -20, 40, 40);
            painter->drawText(QRectF(-15, -15, 30, 30), Qt::AlignCenter, "V");
            painter->drawLine(0, -40, 0, -20);
            painter->drawLine(0, 20, 0, 40);
            break;
        case ComponentType::Diode:
            painter->drawLine(-5, -10, -5, 10); // Cathode line
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
    
    // VISUAL INDICATORS: Red X at terminals
    // Always draw them so user knows where to connect
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

// ============================================================================
// GRID SCENE IMPLEMENTATION
// ============================================================================
GridScene::GridScene(QObject* parent) : QGraphicsScene(parent) {
    setSceneRect(-5000, -5000, 10000, 10000);
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

void GridScene::setTool(ComponentType t) {
    currentTool = t;
    if (ghostItem) {
        removeItem(ghostItem);
        delete ghostItem;
        ghostItem = nullptr;
    }
    if (tempWire) {
        removeItem(tempWire);
        delete tempWire;
        tempWire = nullptr; 
    }
    
    if (t != ComponentType::Select && t != ComponentType::Wire) {
        ghostItem = new SimpleComponent(t);
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
    // Check collision with all items that are SimpleComponents
    QList<QGraphicsItem*> collisions = wire->collidingItems();
    for (auto* item : collisions) {
        if (item == wire || item == ghostItem) continue;
        if (dynamic_cast<SimpleComponent*>(item)) {
            // Found a component body collision
            return false; 
        }
    }
    // Also check self-intersection if we had multiple segments (future)
    return true;
}

bool GridScene::isPlacementValid(const QPointF& pos, QGraphicsItem* item) {
    // Check if drawing/placing here intersects existing items
    QPainterPath shape;
    if (item) {
        shape = item->shape();
        shape.translate(pos); 
    } else {
        shape.addRect(pos.x()-2, pos.y()-2, 4, 4); 
    }

    auto collisions = items(shape);
    for (auto* col : collisions) {
        if (col == item || col == ghostItem || col == tempWire) continue;
        
        // Fail on any collision for now
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
            wireStart = p;
            tempWire = new WireComponent(QLineF(p, p));
            addItem(tempWire);
        }
        else {
            // Placement
            QRectF predictedBounds = ghostItem->boundingRect();
            predictedBounds.translate(p.x(), p.y());
            
            bool colliding = false;
            auto itemsAt = items(predictedBounds, Qt::IntersectsItemBoundingRect);
            for(auto* i : itemsAt) {
                 if (i != ghostItem && i != tempWire) { colliding = true; break; }
            }

            if (!colliding) {
                auto* item = new SimpleComponent(currentTool);
                item->setPos(p);
                item->setRotation(ghostItem->rotation());
                addItem(item);
                emit messageLogged("Placed component.");
            } else {
                emit messageLogged("Invalid Placement: Overlap!");
            }
        }
    } else if (event->button() == Qt::RightButton) {
        setTool(ComponentType::Select);
    }
}

void GridScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QPointF p = snapToGrid(event->scenePos());
    emit mouseMoved((int)p.x(), (int)p.y());

    if (currentTool == ComponentType::Select) {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

    if (tempWire) {
        tempWire->setLine(QLineF(wireStart, p));
        // Visualize Validity
        if (isWireValid(tempWire)) {
             tempWire->setPen(QPen(Qt::cyan, 2));
        } else {
             tempWire->setPen(QPen(Qt::red, 2));
             emit messageLogged("Invalid Wire Path!");
        }
    }
    
    if (ghostItem) {
        ghostItem->setPos(p);
    }
}

void GridScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && tempWire) {
        // Finalize Wire
        if (tempWire->line().length() < 1 || !isWireValid(tempWire)) {
            removeItem(tempWire);
            delete tempWire;
            emit messageLogged("Invalid wire cancelled.");
        } else {
            emit messageLogged("Wire placed.");
        }
        tempWire = nullptr; 
    }
    QGraphicsScene::mouseReleaseEvent(event);
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
                if (auto* sc = dynamic_cast<SimpleComponent*>(item)) {
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
