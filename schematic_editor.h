#pragma once
#include "circuit.h"
#include <QComboBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <set>


/*
 * schematic_editor.h
 * Rudimentary Schematic Editor for CuteApp
 * Allows visual placement of circuit components on a grid.
 */

// ============================================================================
// COMPONENT TYPES
// ============================================================================

enum class SchematicComponentType {
  RESISTOR,
  CAPACITOR,
  DIODE,
  VOLTAGE_SOURCE,
  GROUND
};

// ============================================================================
// COMPONENT ITEM (Visual Representation)
// ============================================================================

class ComponentItem : public QGraphicsRectItem {
public:
  SchematicComponentType type;
  double value;
  int node1GridX, node1GridY; // Terminal 1 grid position
  int node2GridX, node2GridY; // Terminal 2 grid position

  ComponentItem(SchematicComponentType t, double val, int x1, int y1, int x2,
                int y2, QGraphicsItem *parent = nullptr)
      : QGraphicsRectItem(parent), type(t), value(val), node1GridX(x1),
        node1GridY(y1), node2GridX(x2), node2GridY(y2) {

    // Set visual size based on grid spacing
    const int gridSize = 40;
    int minX = std::min(x1, x2) * gridSize;
    int minY = std::min(y1, y2) * gridSize;
    int width = std::abs(x2 - x1) * gridSize + gridSize;
    int height = std::abs(y2 - y1) * gridSize + gridSize;

    setRect(minX, minY, width, height);

    // Color by type
    switch (type) {
    case SchematicComponentType::RESISTOR:
      setBrush(QBrush(QColor(200, 150, 100)));
      break;
    case SchematicComponentType::CAPACITOR:
      setBrush(QBrush(QColor(100, 150, 200)));
      break;
    case SchematicComponentType::DIODE:
      setBrush(QBrush(QColor(200, 100, 100)));
      break;
    case SchematicComponentType::VOLTAGE_SOURCE:
      setBrush(QBrush(QColor(100, 200, 100)));
      break;
    case SchematicComponentType::GROUND:
      setBrush(QBrush(QColor(50, 50, 50)));
      break;
    }
    setPen(QPen(Qt::black, 2));
    setFlag(QGraphicsItem::ItemIsSelectable);
  }
};

// ============================================================================
// SCHEMATIC SCENE
// ============================================================================

class SchematicScene : public QGraphicsScene {
  Q_OBJECT
public:
  std::vector<ComponentItem *> components;
  SchematicComponentType currentTool = SchematicComponentType::RESISTOR;
  double currentValue = 1000.0;

  int placementStartX = -1, placementStartY = -1;
  bool isPlacing = false;

  SchematicScene(QObject *parent = nullptr) : QGraphicsScene(parent) {
    setSceneRect(0, 0, 800, 600);
    drawGrid();
  }

  void drawGrid() {
    const int gridSize = 40;
    QPen gridPen(QColor(220, 220, 220), 1);
    for (int x = 0; x <= 800; x += gridSize) {
      addLine(x, 0, x, 600, gridPen);
    }
    for (int y = 0; y <= 600; y += gridSize) {
      addLine(0, y, 800, y, gridPen);
    }
  }

  void setTool(SchematicComponentType t, double val) {
    currentTool = t;
    currentValue = val;
  }

protected:
  void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

public:
  /**
   * toNetlist
   * Converts the visual schematic into a TensorNetlist for simulation.
   */
  TensorNetlist toNetlist() const {
    TensorNetlist netlist;
    std::map<std::pair<int, int>, int> nodeMap; // Grid coord -> Node ID
    int nextNodeId = 1;                         // 0 is reserved for ground

    // Helper to get or assign a node ID
    auto getNode = [&](int gx, int gy) -> int {
      // Ground is always node 0 (convention: bottom row or explicit ground)
      if (gy >= 14)
        return 0; // Bottom of grid is ground

      auto key = std::make_pair(gx, gy);
      if (nodeMap.find(key) == nodeMap.end()) {
        nodeMap[key] = nextNodeId++;
      }
      return nodeMap[key];
    };

    for (const auto *c : components) {
      int n1 = getNode(c->node1GridX, c->node1GridY);
      int n2 = getNode(c->node2GridX, c->node2GridY);

      switch (c->type) {
      case SchematicComponentType::RESISTOR:
        netlist.addResistor(n1, n2, c->value);
        break;
      case SchematicComponentType::CAPACITOR:
        netlist.addCapacitor(n1, n2, c->value);
        break;
      case SchematicComponentType::DIODE:
        netlist.addDiode(n1, n2, 1e-14); // Is, N=1, Vt=25.85mV default
        break;
      case SchematicComponentType::VOLTAGE_SOURCE:
        netlist.addVoltageSource(n1, n2, c->value);
        break;
      case SchematicComponentType::GROUND:
        // Ground marker, no action needed
        break;
      }
    }
    return netlist;
  }
};

// ============================================================================
// SCHEMATIC EDITOR WIDGET
// ============================================================================

class SchematicEditor : public QWidget {
  Q_OBJECT
public:
  SchematicScene *scene;
  QGraphicsView *view;
  QComboBox *toolSelector;
  QLineEdit *valueInput;

  SchematicEditor(QWidget *parent = nullptr);

  TensorNetlist toNetlist() const { return scene->toNetlist(); }

signals:
  void netlistChanged();
};
