#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QGroupBox>
#include <QMap>
#include <QKeyEvent>
#include "circuit.h"

// ============================================================================
// CONSTANTS & ENUMS
// ============================================================================
const int GRID_STEP = 20;

enum class ComponentType {
    Select,
    Wire,
    Resistor,
    Capacitor,
    Inductor,
    VoltageSource,
    Ground,
    Diode,
    Mosfet,
    Bjt
};

// ============================================================================
// WIRE COMPONENT
// ============================================================================
class WireComponent : public QGraphicsLineItem {
public:
    WireComponent(QLineF line);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;
    
    // Snapping support
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
};

// ============================================================================
// SIMPLE COMPONENT
// ============================================================================
class SimpleComponent : public QGraphicsItem {
public:
    ComponentType type;
    SimpleComponent(ComponentType t);

    QRectF boundingRect() const override;
    QPainterPath shape() const override; // For precise collision
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;
    
    QVector<QPointF> getTerminals() const; // Helper for alignment/drawing

    // Snapping support
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    
    void rotate90(); 
};

// ============================================================================
// SCENE (Grid Handling + Validation)
// ============================================================================
class GridScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit GridScene(QObject* parent = nullptr);

    void setTool(ComponentType t);
    void setGridVisible(bool visible);
    void setSnappingEnabled(bool enabled);
    bool isSnappingEnabled() const { return snappingEnabled; }
    
    TensorNetlist generateNetlist();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

signals:
    void mouseMoved(int x, int y);
    void messageLogged(const QString& msg);
    void toolChanged(ComponentType newTool);

private:
    QPointF snapToGrid(const QPointF& pos);
    bool isPlacementValid(const QPointF& pos, QGraphicsItem* item);
    bool isWireValid(QGraphicsLineItem* wire); // New validation

    bool gridVisible = true;
    bool snappingEnabled = true;

    ComponentType currentTool = ComponentType::Select;
    
    // Wire drawing
    QPointF wireStart;
    WireComponent* tempWire = nullptr;
    
    // Ghost for placement
    SimpleComponent* ghostItem = nullptr; 
};

// ============================================================================
// MAIN EDITOR WIDGET
// ============================================================================
class SchematicEditor : public QWidget {
    Q_OBJECT
public:
    explicit SchematicEditor(QWidget* parent = nullptr);
    TensorNetlist toNetlist() { return scene->generateNetlist(); }

private slots:
    void onGridToggled(bool checked);
    void onSnappingToggled(bool checked);

private:
    GridScene* scene;
    QGraphicsView* view;
    QLabel* statusLabel;
    
    void setupUi();
    QWidget* createRibbon();
    QPushButton* createToolButton(const QString& text, ComponentType type);
    QMap<ComponentType, QPushButton*> toolButtons;
};
