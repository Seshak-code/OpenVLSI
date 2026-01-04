#pragma once

#include "circuit.h"
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <vector>


// ============================================================================
// ENUM + UTILITIES
// ============================================================================

enum class SchematicComponentType {
  WIRE,
  RESISTOR,
  CAPACITOR,
  DIODE,
  MOSFET,
  BJT,
  VOLTAGE_SOURCE,
  GROUND,
  SELECT,
  PAN,
  DELETE_TOOL
};

const int GRID_SIZE = 20;
const int LAYER_WIRES = 10;
const int LAYER_COMPONENTS = 20;

struct GridPoint {
  int x, y;
  bool operator<(const GridPoint &other) const {
    return std::tie(x, y) < std::tie(other.x, other.y);
  }
  bool operator==(const GridPoint &other) const {
    return x == other.x && y == other.y;
  }
};

// ============================================================================
// CONNECTIVITY MANAGER (Union-Find)
// ============================================================================

class ConnectivityManager {
  std::map<GridPoint, GridPoint> parent;

public:
  GridPoint find(GridPoint p) {
    if (parent.find(p) == parent.end())
      return parent[p] = p;
    if (parent[p] == p)
      return p;
    return parent[p] = find(parent[p]);
  }

  void unite(GridPoint a, GridPoint b) {
    GridPoint rootA = find(a);
    GridPoint rootB = find(b);
    if (!(rootA == rootB)) {
      parent[rootA] = rootB;
    }
  }
};

// ============================================================================
// VISUAL ITEMS
// ============================================================================

class WireItem : public QGraphicsLineItem {
public:
  GridPoint start, end;
  WireItem(GridPoint s, GridPoint e, QGraphicsItem *parent = nullptr);
};

class ComponentItem : public QGraphicsItem {
public:
  SchematicComponentType type;
  QString value;
  GridPoint pos; // Anchor grid point
  std::vector<GridPoint> terminals;

  ComponentItem(SchematicComponentType t, const QString &val, GridPoint p,
                QGraphicsItem *parent = nullptr);

  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override;

  QVariant itemChange(GraphicsItemChange change,
                      const QVariant &value) override;

private:
  void setupTerminals();
  void drawResistor(QPainter *painter);
  void drawCapacitor(QPainter *painter);
  void drawDiode(QPainter *painter);
  void drawVoltageSource(QPainter *painter);
  void drawGround(QPainter *painter);
  void drawMosfet(QPainter *painter);
  void drawBjt(QPainter *painter);
};

// ============================================================================
// SCENE + EDITOR
// ============================================================================

class SchematicScene : public QGraphicsScene {
  Q_OBJECT
public:
  explicit SchematicScene(QObject *parent = nullptr);

  void setTool(SchematicComponentType tool, double val = 0) {
    currentTool = tool;
    componentValue = QString::number(val);
  }
  void setComponentValue(const QString &val) { componentValue = val; }
  TensorNetlist toNetlist() const;

signals:
  void coordinateChanged(GridPoint p);

protected:
  void drawBackground(QPainter *painter, const QRectF &rect) override;
  void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  SchematicComponentType currentTool = SchematicComponentType::SELECT;
  QString componentValue = "1000";
  bool isPlacing = false;
  GridPoint placementStart;
  QGraphicsLineItem *previewWire = nullptr;
  ComponentItem *previewComponent = nullptr;

  std::vector<ComponentItem *> components;
  std::vector<WireItem *> wires;

  void updatePreview(GridPoint p);
  void finalizePlacement(GridPoint p);
};

class SchematicEditor : public QWidget {
  Q_OBJECT
public:
  explicit SchematicEditor(QWidget *parent = nullptr);

  void zoomIn();
  void zoomOut();
  void zoomFit();
  TensorNetlist toNetlist() { return scene->toNetlist(); }

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  SchematicScene *scene;
  QGraphicsView *view;
  QLineEdit *valueInput;
  QLabel *statusCoords;
  QLabel *statusInfo;

  void setupTopToolbar(QHBoxLayout *layout);
  void setupLeftToolbar(QVBoxLayout *layout);
  void setupRightToolbar(QVBoxLayout *layout);
  void setupStatusBar(QHBoxLayout *layout);
  QPushButton *makeToolButton(const QString &text, SchematicComponentType type,
                              bool isPlacement = true);
};
