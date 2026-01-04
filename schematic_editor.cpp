#include "schematic_editor.h"
#include <QCoreApplication>
#include <QFrame>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>

// GRID_SIZE, LAYER_WIRES, LAYER_COMPONENTS are defined in schematic_editor.h

// ============================================================================
// WIRE ITEM
// ============================================================================

WireItem::WireItem(GridPoint s, GridPoint e, QGraphicsItem *parent)
    : QGraphicsLineItem(parent), start(s), end(e) {
  setLine(s.x * GRID_SIZE, s.y * GRID_SIZE, e.x * GRID_SIZE, e.y * GRID_SIZE);
  // Cyan wire with 80% opacity for Unreal/Unity style
  setPen(QPen(QColor(0, 191, 255, 200), 2));
  setZValue(LAYER_WIRES);
  setFlag(QGraphicsItem::ItemIsSelectable);
}

// ============================================================================
// COMPONENT ITEM
// ============================================================================

ComponentItem::ComponentItem(SchematicComponentType t, const QString &v,
                             GridPoint p, QGraphicsItem *parent)
    : QGraphicsItem(parent), type(t), value(v), pos(p) {
  setZValue(LAYER_COMPONENTS);
  setupTerminals();
  setFlag(QGraphicsItem::ItemIsSelectable);
  setFlag(QGraphicsItem::ItemIsMovable);
  setFlag(QGraphicsItem::ItemSendsGeometryChanges);
  setPos(pos.x * GRID_SIZE, pos.y * GRID_SIZE);
}

void ComponentItem::setupTerminals() {
  switch (type) {
  case SchematicComponentType::RESISTOR:
  case SchematicComponentType::CAPACITOR:
  case SchematicComponentType::DIODE:
  case SchematicComponentType::VOLTAGE_SOURCE:
    terminals = {pos, {pos.x + 4, pos.y}};
    break;
  case SchematicComponentType::MOSFET:
  case SchematicComponentType::BJT:
    terminals = {pos, {pos.x, pos.y + 4}, {pos.x + 4, pos.y + 2}};
    break;
  case SchematicComponentType::GROUND:
    terminals = {pos};
    break;
  default:
    break;
  }
}
QVariant ComponentItem::itemChange(GraphicsItemChange change,
                                   const QVariant &value) {
  if (change == ItemPositionChange && scene()) {
    QPointF newPos = value.toPointF();
    qreal x = std::round(newPos.x() / GRID_SIZE) * GRID_SIZE;
    qreal y = std::round(newPos.y() / GRID_SIZE) * GRID_SIZE;
    return QPointF(x, y);
  }
  return QGraphicsItem::itemChange(change, value);
}

QRectF ComponentItem::boundingRect() const {
  return QRectF(0, 0, 3 * GRID_SIZE, 3 * GRID_SIZE).adjusted(-10, -10, 10, 10);
}

void ComponentItem::paint(QPainter *painter,
                          const QStyleOptionGraphicsItem *option,
                          QWidget *widget) {
  Q_UNUSED(option);
  Q_UNUSED(widget);
  painter->setRenderHint(QPainter::Antialiasing);

  if (isSelected()) {
    painter->setPen(
        QPen(QColor(0, 122, 204), 2, Qt::DashLine)); // Blue accent selection
    painter->drawRect(boundingRect());
  }

  painter->setPen(QPen(QColor(170, 170, 170), 2)); // Light gray for dark theme
  painter->setBrush(Qt::NoBrush);

  switch (type) {
  case SchematicComponentType::RESISTOR:
    drawResistor(painter);
    break;
  case SchematicComponentType::CAPACITOR:
    drawCapacitor(painter);
    break;
  case SchematicComponentType::DIODE:
    drawDiode(painter);
    break;
  case SchematicComponentType::VOLTAGE_SOURCE:
    drawVoltageSource(painter);
    break;
  case SchematicComponentType::GROUND:
    drawGround(painter);
    break;
  case SchematicComponentType::MOSFET:
    drawMosfet(painter);
    break;
  case SchematicComponentType::BJT:
    drawBjt(painter);
    break;
  default:
    break;
  }

  painter->setBrush(QColor(0, 191, 255)); // Cyan terminal dots
  painter->setPen(Qt::NoPen);
  for (const auto &t : terminals) {
    painter->drawEllipse(
        QPointF((t.x - pos.x) * GRID_SIZE, (t.y - pos.y) * GRID_SIZE), 3, 3);
  }
}

void ComponentItem::drawResistor(QPainter *painter) {
  painter->setPen(QPen(Qt::black, 2));
  painter->drawRect(10, 10, 60, 20);
  painter->drawLine(0, 15, 10, 15);
  painter->drawLine(70, 15, 80, 15);
}

void ComponentItem::drawCapacitor(QPainter *painter) {
  painter->setPen(QPen(Qt::black, 2));
  painter->drawLine(0, 15, 35, 15);
  painter->drawLine(45, 15, 80, 15);
  painter->drawLine(35, 5, 35, 25);
  painter->drawLine(45, 5, 45, 25);
}

void ComponentItem::drawDiode(QPainter *painter) {
  painter->setPen(QPen(Qt::black, 2));
  painter->drawLine(0, 15, 30, 15);
  painter->drawLine(50, 15, 80, 15);
  static const QPointF points[3] = {{30, 5}, {50, 15}, {30, 25}};
  painter->setBrush(Qt::black);
  painter->drawPolygon(points, 3);
  painter->drawLine(50, 5, 50, 25);
}

void ComponentItem::drawVoltageSource(QPainter *painter) {
  painter->setPen(QPen(Qt::black, 2));
  painter->drawEllipse(20, 0, 40, 40);
  painter->drawLine(0, 20, 20, 20);
  painter->drawLine(60, 20, 80, 20);
  painter->drawText(QRectF(20, 0, 40, 40), Qt::AlignCenter, "V");
}

void ComponentItem::drawGround(QPainter *painter) {
  painter->setPen(QPen(Qt::black, 2));
  painter->drawLine(0, 0, 0, 20);
  painter->drawLine(-15, 20, 15, 20);
  painter->drawLine(-10, 25, 10, 25);
  painter->drawLine(-5, 30, 5, 30);
}

void ComponentItem::drawMosfet(QPainter *painter) {
  painter->setPen(QPen(Qt::black, 2));
  painter->drawLine(0, 0, 0, 80);
  painter->drawLine(-20, 40, 0, 40);
  painter->drawLine(20, 10, 20, 70);
  painter->drawLine(20, 10, 80, 0);
  painter->drawLine(20, 70, 80, 80);
}

void ComponentItem::drawBjt(QPainter *painter) {
  painter->setPen(QPen(Qt::black, 2));
  painter->drawLine(10, 10, 10, 70);
  painter->drawLine(-10, 40, 10, 40);
  painter->drawLine(10, 20, 80, 0);
  painter->drawLine(10, 60, 80, 80);
}

// ============================================================================
// SCENE IMPLEMENTATION
// ============================================================================

SchematicScene::SchematicScene(QObject *parent) : QGraphicsScene(parent) {
  setSceneRect(0, 0, 4000, 4000);
}

void SchematicScene::drawBackground(QPainter *painter, const QRectF &rect) {
  // Unreal/Unity style dark canvas
  painter->fillRect(rect, QColor(30, 30, 30));

  const int gridSize = 20;
  const int majorGridSize = gridSize * 5;

  painter->save();

  qreal left = std::floor(rect.left() / gridSize) * gridSize;
  qreal right = std::ceil(rect.right() / gridSize) * gridSize;
  qreal top = std::floor(rect.top() / gridSize) * gridSize;
  qreal bottom = std::ceil(rect.bottom() / gridSize) * gridSize;

  // Minor grid - subtle
  painter->setPen(QPen(QColor(45, 45, 45), 0));
  for (qreal x = left; x <= right; x += gridSize)
    painter->drawLine(QPointF(x, top), QPointF(x, bottom));
  for (qreal y = top; y <= bottom; y += gridSize)
    painter->drawLine(QPointF(left, y), QPointF(right, y));

  // Major grid - slightly more visible
  painter->setPen(QPen(QColor(58, 58, 58), 0));
  qreal mLeft = std::floor(rect.left() / majorGridSize) * majorGridSize;
  qreal mTop = std::floor(rect.top() / majorGridSize) * majorGridSize;
  for (qreal x = mLeft; x <= right; x += majorGridSize)
    painter->drawLine(QPointF(x, top), QPointF(x, bottom));
  for (qreal y = mTop; y <= bottom; y += majorGridSize)
    painter->drawLine(QPointF(left, y), QPointF(right, y));

  painter->restore();
}

void SchematicScene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  GridPoint p = {(int)std::round(event->scenePos().x() / GRID_SIZE),
                 (int)std::round(event->scenePos().y() / GRID_SIZE)};

  if (event->button() == Qt::LeftButton) {
    if (currentTool == SchematicComponentType::SELECT) {
      QGraphicsScene::mousePressEvent(event);
    } else if (currentTool == SchematicComponentType::DELETE_TOOL) {
      auto itemsAtPos = items(event->scenePos());
      for (auto *item : itemsAtPos) {
        auto itComp = std::find(components.begin(), components.end(),
                                (ComponentItem *)item);
        if (itComp != components.end())
          components.erase(itComp);
        auto itWire = std::find(wires.begin(), wires.end(), (WireItem *)item);
        if (itWire != wires.end())
          wires.erase(itWire);
        removeItem(item);
        delete item;
      }
    } else {
      placementStart = p;
      isPlacing = true;
    }
  } else {
    QGraphicsScene::mousePressEvent(event);
  }
}

void SchematicScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
  GridPoint p = {(int)std::round(event->scenePos().x() / GRID_SIZE),
                 (int)std::round(event->scenePos().y() / GRID_SIZE)};
  emit coordinateChanged(p);
  QGraphicsScene::mouseMoveEvent(event);
}

void SchematicScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
  if (event->button() == Qt::LeftButton && isPlacing) {
    GridPoint end = {(int)std::round(event->scenePos().x() / GRID_SIZE),
                     (int)std::round(event->scenePos().y() / GRID_SIZE)};
    if (currentTool == SchematicComponentType::WIRE) {
      if (!(placementStart == end)) {
        // Manhattan Routing (L-bend)
        if (placementStart.x != end.x && placementStart.y != end.y) {
          // Create horizontal then vertical
          // segment
          GridPoint mid = {end.x, placementStart.y};
          auto *hWire = new WireItem(placementStart, mid);
          auto *vWire = new WireItem(mid, end);
          addItem(hWire);
          wires.push_back(hWire);
          addItem(vWire);
          wires.push_back(vWire);
        } else {
          auto *wire = new WireItem(placementStart, end);
          addItem(wire);
          wires.push_back(wire);
        }
      }
    } else if (currentTool != SchematicComponentType::SELECT &&
               currentTool != SchematicComponentType::PAN &&
               currentTool != SchematicComponentType::DELETE_TOOL) {
      auto *comp =
          new ComponentItem(currentTool, componentValue, placementStart);
      addItem(comp);
      components.push_back(comp);
      comp->setPos(placementStart.x * GRID_SIZE, placementStart.y * GRID_SIZE);
    }
    isPlacing = false;
  }
  QGraphicsScene::mouseReleaseEvent(event);
}

void SchematicScene::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
    for (auto *item : selectedItems()) {
      auto itComp = std::find(components.begin(), components.end(),
                              (ComponentItem *)item);
      if (itComp != components.end())
        components.erase(itComp);
      auto itWire = std::find(wires.begin(), wires.end(), (WireItem *)item);
      if (itWire != wires.end())
        wires.erase(itWire);
      removeItem(item);
      delete item;
    }
  } else {
    QGraphicsScene::keyPressEvent(event);
  }
}

TensorNetlist SchematicScene::toNetlist() const {
  TensorNetlist netlist;
  ConnectivityManager cm;
  for (const auto *w : wires)
    cm.unite(w->start, w->end);

  std::set<GridPoint> grounds;
  for (const auto *c : components) {
    GridPoint currentPos = {(int)std::round(c->x() / GRID_SIZE),
                            (int)std::round(c->y() / GRID_SIZE)};
    if (c->type == SchematicComponentType::GROUND)
      grounds.insert(cm.find(currentPos));
  }

  std::map<GridPoint, int> netMap;
  int nextNodeId = 1;
  auto getNetId = [&](GridPoint p) -> int {
    GridPoint root = cm.find(p);
    if (grounds.count(root))
      return 0;
    if (netMap.find(root) == netMap.end())
      netMap[root] = nextNodeId++;
    return netMap[root];
  };

  for (const auto *c : components) {
    if (c->type == SchematicComponentType::GROUND)
      continue;
    GridPoint curPos = {(int)std::round(c->x() / GRID_SIZE),
                        (int)std::round(c->y() / GRID_SIZE)};
    std::vector<int> nodes;
    for (const auto &t : c->terminals) {
      GridPoint shiftedT = {curPos.x + (t.x - c->pos.x),
                            curPos.y + (t.y - c->pos.y)};
      nodes.push_back(getNetId(shiftedT));
    }
    if (nodes.size() < 2 && c->type != SchematicComponentType::GROUND)
      continue;

    switch (c->type) {
    case SchematicComponentType::RESISTOR:
      netlist.addResistor(nodes[0], nodes[1], c->value.toDouble());
      break;
    case SchematicComponentType::CAPACITOR:
      netlist.addCapacitor(nodes[0], nodes[1], c->value.toDouble());
      break;
    case SchematicComponentType::DIODE:
      netlist.addDiode(nodes[0], nodes[1], 1e-14);
      break;
    case SchematicComponentType::VOLTAGE_SOURCE:
      netlist.addVoltageSource(nodes[0], nodes[1], c->value.toDouble());
      break;
    case SchematicComponentType::MOSFET:
      netlist.addMosfet("M" +
                            std::to_string(netlist.globalBlock.mosfets.size()),
                        nodes[0], nodes[1], nodes[2], 0, "DEFAULT", 1e-4, 1e-6);
      break;
    case SchematicComponentType::BJT:
      netlist.addBJT("Q" + std::to_string(netlist.globalBlock.bjts.size()),
                     nodes[0], nodes[1], nodes[2], "DEFAULT", true);
      break;
    default:
      break;
    }
  }
  return netlist;
}

// ============================================================================
// EDITOR WIDGET
// ============================================================================

SchematicEditor::SchematicEditor(QWidget *parent) : QWidget(parent) {
  // Unreal/Unity dark professional theme
  setStyleSheet("background-color: #1E1E1E; color: #CCCCCC; font-family: "
                "'Segoe UI', sans-serif;");

  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Top Toolbar - Dark panel
  auto *topContainer = new QWidget(this);
  topContainer->setFixedHeight(40);
  topContainer->setStyleSheet(
      "background-color: #252526; border-bottom: 1px solid #3C3C3C;");
  auto *topLayout = new QHBoxLayout(topContainer);
  topLayout->setContentsMargins(8, 0, 8, 0);
  setupTopToolbar(topLayout);
  mainLayout->addWidget(topContainer);

  auto *middleLayout = new QHBoxLayout();
  middleLayout->setSpacing(0);

  // Left Toolbar - Dark panel
  auto *leftContainer = new QWidget(this);
  leftContainer->setFixedWidth(40);
  leftContainer->setStyleSheet(
      "background-color: #252526; border-right: 1px solid #3C3C3C;");
  auto *leftLayout = new QVBoxLayout(leftContainer);
  leftLayout->setContentsMargins(4, 8, 4, 8);
  leftLayout->setAlignment(Qt::AlignTop);
  setupLeftToolbar(leftLayout);
  middleLayout->addWidget(leftContainer);

  // Center Scene
  scene = new SchematicScene(this);
  view = new QGraphicsView(scene, this);
  view->setRenderHint(QPainter::Antialiasing);
  view->setBackgroundBrush(Qt::transparent);
  view->setFrameShape(QFrame::NoFrame);
  view->setDragMode(QGraphicsView::NoDrag);
  view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  view->installEventFilter(this);
  middleLayout->addWidget(view);

  // Right Toolbar - Dark panel
  auto *rightContainer = new QWidget(this);
  rightContainer->setFixedWidth(40);
  rightContainer->setStyleSheet(
      "background-color: #252526; border-left: 1px solid #3C3C3C;");
  auto *rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setContentsMargins(4, 8, 4, 8);
  rightLayout->setAlignment(Qt::AlignTop);
  setupRightToolbar(rightLayout);
  middleLayout->addWidget(rightContainer);

  mainLayout->addLayout(middleLayout);

  // Status Bar - Dark panel
  auto *statusContainer = new QWidget(this);
  statusContainer->setFixedHeight(24);
  statusContainer->setStyleSheet(
      "background-color: #007ACC; border-top: 1px solid #005A9E;");
  auto *statusLayout = new QHBoxLayout(statusContainer);
  statusLayout->setContentsMargins(10, 0, 10, 0);
  setupStatusBar(statusLayout);
  mainLayout->addWidget(statusContainer);

  connect(scene, &SchematicScene::coordinateChanged, this, [=](GridPoint p) {
    statusCoords->setText(QString("X: %1  Y: %2").arg(p.x).arg(p.y));
  });
}

void SchematicEditor::setupTopToolbar(QHBoxLayout *layout) {
  auto *title = new QLabel("AcuteSim Schematic", this);
  title->setStyleSheet("font-weight: bold; color: #333; "
                       "margin-right: 20px;");
  layout->addWidget(title);

  // Zoom Controls
  auto *btnIn = new QPushButton("+", this);
  btnIn->setFixedSize(30, 30);
  btnIn->setToolTip("Zoom In");
  btnIn->setStyleSheet("background-color: #EEE; border: 1px "
                       "solid #AAA; color: black;");
  connect(btnIn, &QPushButton::clicked, this, &SchematicEditor::zoomIn);
  layout->addWidget(btnIn);

  auto *btnOut = new QPushButton("-", this);
  btnOut->setFixedSize(30, 30);
  btnOut->setToolTip("Zoom Out");
  btnOut->setStyleSheet("background-color: #EEE; border: 1px "
                        "solid #AAA; color: black;");
  connect(btnOut, &QPushButton::clicked, this, &SchematicEditor::zoomOut);
  layout->addWidget(btnOut);

  auto *btnFit = new QPushButton("Fit", this);
  btnFit->setFixedSize(40, 30);
  btnFit->setToolTip("Zoom to Fit");
  btnFit->setStyleSheet("background-color: #EEE; border: 1px "
                        "solid #AAA; "
                        "color: black; font-size: 10px;");
  connect(btnFit, &QPushButton::clicked, this, &SchematicEditor::zoomFit);
  layout->addWidget(btnFit);

  layout->addSpacing(20);

  valueInput = new QLineEdit("1000", this);
  valueInput->setFixedWidth(100);
  valueInput->setPlaceholderText("Value");
  valueInput->setStyleSheet("background-color: #FFF; border: 1px "
                            "solid "
                            "#AAA; padding: 2px; color: #000;");
  layout->addWidget(new QLabel("Value:", this));
  layout->addWidget(valueInput);

  layout->addStretch();
}

void SchematicEditor::zoomIn() { view->scale(1.2, 1.2); }

void SchematicEditor::zoomOut() { view->scale(1.0 / 1.2, 1.0 / 1.2); }

void SchematicEditor::zoomFit() {
  view->resetTransform();
  view->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

bool SchematicEditor::eventFilter(QObject *watched, QEvent *event) {
  if (watched == view && event->type() == QEvent::Wheel) {
    auto *wheelEvent = static_cast<QWheelEvent *>(event);
    if (wheelEvent->modifiers() & Qt::ControlModifier) {
      if (wheelEvent->angleDelta().y() > 0)
        zoomIn();
      else
        zoomOut();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void SchematicEditor::setupLeftToolbar(QVBoxLayout *layout) {
  auto *sel = makeToolButton("S", SchematicComponentType::SELECT, false);
  sel->setToolTip("Selection Tool");
  sel->setChecked(true);
  layout->addWidget(sel);

  auto *pan = makeToolButton("P", SchematicComponentType::PAN, false);
  pan->setToolTip("Pan View");
  layout->addWidget(pan);

  auto *del = makeToolButton("D", SchematicComponentType::DELETE_TOOL, false);
  del->setToolTip("Delete Tool");
  layout->addWidget(del);
}

void SchematicEditor::setupRightToolbar(QVBoxLayout *layout) {
  layout->addWidget(makeToolButton("W", SchematicComponentType::WIRE));
  layout->addWidget(makeToolButton("R", SchematicComponentType::RESISTOR));
  layout->addWidget(makeToolButton("C", SchematicComponentType::CAPACITOR));
  layout->addWidget(makeToolButton("D", SchematicComponentType::DIODE));
  layout->addWidget(makeToolButton("M", SchematicComponentType::MOSFET));
  layout->addWidget(makeToolButton("Q", SchematicComponentType::BJT));
  layout->addWidget(
      makeToolButton("V", SchematicComponentType::VOLTAGE_SOURCE));
  layout->addWidget(makeToolButton("G", SchematicComponentType::GROUND));
}

void SchematicEditor::setupStatusBar(QHBoxLayout *layout) {
  statusCoords = new QLabel("X: 0  Y: 0", this);
  statusCoords->setFixedWidth(120);
  statusCoords->setStyleSheet("color: white; font-size: 11px;");
  layout->addWidget(statusCoords);

  statusInfo = new QLabel("Ready", this);
  statusInfo->setStyleSheet("color: white; font-size: 11px;");
  layout->addWidget(statusInfo);

  layout->addStretch();
  auto *gridLabel = new QLabel("Grid: 20px", this);
  gridLabel->setStyleSheet("color: white; font-size: 11px;");
  layout->addWidget(gridLabel);
}

QPushButton *SchematicEditor::makeToolButton(const QString &text,
                                             SchematicComponentType type,
                                             bool isPlacement) {
  auto *btn = new QPushButton(text, this);
  btn->setFixedSize(32, 32);
  btn->setCheckable(true);
  btn->setCursor(Qt::PointingHandCursor);

  // Unreal/Unity style flat buttons with glow hover
  QString baseStyle = R"(
    QPushButton {
      background-color: transparent;
      border: 1px solid transparent;
      border-radius: 4px;
      font-weight: bold;
      font-size: 12px;
      color: #AAAAAA;
    }
    QPushButton:hover {
      background-color: #3E3E42;
      border: 1px solid #007ACC;
      color: #FFFFFF;
    }
    QPushButton:checked {
      background-color: #007ACC;
      border: 1px solid #005A9E;
      color: #FFFFFF;
    }
  )";
  btn->setStyleSheet(baseStyle);

  connect(btn, &QPushButton::clicked, this, [=]() {
    scene->setTool(type, valueInput->text().toDouble());

    // Uncheck others
    for (auto *other : findChildren<QPushButton *>()) {
      if (other != btn)
        other->setChecked(false);
    }

    if (type == SchematicComponentType::PAN) {
      view->setDragMode(QGraphicsView::ScrollHandDrag);
    } else {
      view->setDragMode(QGraphicsView::NoDrag);
    }

    statusInfo->setText(isPlacement ? "Placing Component" : "Tool Active");
  });

  return btn;
}
