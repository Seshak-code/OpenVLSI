#include "probe_tip.h"
#include "visual_component.h"
#include "../editors/schematic/schematic_editor.h" // For GridScene access if needed
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QDebug>
#include <cmath>

// Constants for tip appearance
const double TIP_RADIUS = 6.0;

ProbeTip::ProbeTip(TipType type, VisualComponent* parentComponent)
    : QGraphicsObject(parentComponent), type_(type), parentComponent_(parentComponent)
{
    setFlag(ItemIsMovable, true);
    setFlag(ItemIsSelectable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setAcceptHoverEvents(true);
    
    // Set color based on type
    if (type == Positive) {
        color_ = Qt::red;
        setToolTip("Probe Positive (+)");
    } else {
        color_ = Qt::black;
        setToolTip("Probe Negative (-)");
    }
    
    // Initial position relative to parent (will be adjusted by parent)
    setZValue(100); // Draw on top of wires
}

QRectF ProbeTip::boundingRect() const
{
    // Hit test area slightly larger than visual
    return QRectF(-TIP_RADIUS - 2, -TIP_RADIUS - 2, 2*TIP_RADIUS + 4, 2*TIP_RADIUS + 4);
}

QPainterPath ProbeTip::shape() const
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), TIP_RADIUS, TIP_RADIUS);
    return path;
}

void ProbeTip::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setPen(QPen(Qt::white, 1.0));
    painter->setBrush(QBrush(color_));
    
    // Draw the tip (circle)
    painter->drawEllipse(QPointF(0, 0), TIP_RADIUS, TIP_RADIUS);
    
    // Draw crosshair or indicator
    painter->setPen(QPen(Qt::white, 1.5));
    if (type_ == Positive) {
        painter->drawLine(0, -3, 0, 3);
        painter->drawLine(-3, 0, 3, 0);
    } else {
        painter->drawLine(-3, 0, 3, 0);
    }
}

QVariant ProbeTip::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        QPointF newPos = value.toPointF();
        
        // Snap to grid (assuming 10px or 20px grid from scene)
        // We can access scene properties or just hardcode grid step for now
        // Ideally we static cast scene() to GridScene but for decoupling we use local math
        double gridStep = 20.0; 
        
        double x = std::round(newPos.x() / gridStep) * gridStep;
        double y = std::round(newPos.y() / gridStep) * gridStep;
        
        return QPointF(x, y);
    }
    return QGraphicsObject::itemChange(change, value);
}

void ProbeTip::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    isDragging_ = true;
    setCursor(Qt::ClosedHandCursor);
    QGraphicsObject::mousePressEvent(event);
}

void ProbeTip::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (isDragging_) {
        // Default implementation handles movement because ItemIsMovable is set
        // But we want to ensure we stay within scene bounds or do custom logic
        QGraphicsObject::mouseMoveEvent(event);
        emit tipMoved(pos());
        // Force parent to redraw the connecting line
        if (parentComponent_) parentComponent_->update();
    }
}

void ProbeTip::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    isDragging_ = false;
    setCursor(Qt::ArrowCursor);
    QGraphicsObject::mouseReleaseEvent(event);
    
    // Here we will eventually snap to nearest wire if close enough
    // For now, grid snapping in itemChange covers basic placement
}
