#include "schematic_editor.h"
#include <QGraphicsSceneMouseEvent>
#include <QLineEdit>

// ============================================================================
// SCHEMATIC SCENE IMPLEMENTATION
// ============================================================================

void SchematicScene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    const int gridSize = 40;
    placementStartX = (int)(event->scenePos().x() / gridSize);
    placementStartY = (int)(event->scenePos().y() / gridSize);
    isPlacing = true;
  }
  QGraphicsScene::mousePressEvent(event);
}

void SchematicScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
  if (event->button() == Qt::LeftButton && isPlacing) {
    const int gridSize = 40;
    int endX = (int)(event->scenePos().x() / gridSize);
    int endY = (int)(event->scenePos().y() / gridSize);

    // Require at least 1 grid cell span for 2-terminal components
    if (placementStartX != endX || placementStartY != endY) {
      auto *comp = new ComponentItem(currentTool, currentValue, placementStartX,
                                     placementStartY, endX, endY);
      addItem(comp);
      components.push_back(comp);
    }
    isPlacing = false;
  }
  QGraphicsScene::mouseReleaseEvent(event);
}

// ============================================================================
// SCHEMATIC EDITOR WIDGET IMPLEMENTATION
// ============================================================================

SchematicEditor::SchematicEditor(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Toolbar
  auto *toolbar = new QHBoxLayout();

  toolSelector = new QComboBox(this);
  toolSelector->addItem("Resistor", (int)SchematicComponentType::RESISTOR);
  toolSelector->addItem("Capacitor", (int)SchematicComponentType::CAPACITOR);
  toolSelector->addItem("Diode", (int)SchematicComponentType::DIODE);
  toolSelector->addItem("Voltage Source",
                        (int)SchematicComponentType::VOLTAGE_SOURCE);
  toolbar->addWidget(new QLabel("Component:", this));
  toolbar->addWidget(toolSelector);

  valueInput = new QLineEdit("1000", this);
  valueInput->setMaximumWidth(80);
  toolbar->addWidget(new QLabel("Value:", this));
  toolbar->addWidget(valueInput);

  toolbar->addStretch();
  layout->addLayout(toolbar);

  // Scene and View
  scene = new SchematicScene(this);
  view = new QGraphicsView(scene, this);
  view->setRenderHint(QPainter::Antialiasing);
  view->setMinimumSize(400, 300);
  layout->addWidget(view);

  // Connect tool selection
  connect(toolSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            auto type = static_cast<SchematicComponentType>(
                toolSelector->itemData(index).toInt());
            double val = valueInput->text().toDouble();
            scene->setTool(type, val);
          });

  connect(
      valueInput, &QLineEdit::textChanged, this,
      [this](const QString &text) { scene->currentValue = text.toDouble(); });
}
