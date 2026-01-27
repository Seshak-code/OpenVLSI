#pragma once

#include <QGraphicsObject>
#include <QBrush>
#include <QPen>

class VisualComponent; // Forward declaration

class ProbeTip : public QGraphicsObject {
    Q_OBJECT
public:
    enum TipType { Positive, Negative };

    ProbeTip(TipType type, VisualComponent* parentComponent);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;

    TipType getType() const { return type_; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void tipMoved(const QPointF& newPos);

private:
    TipType type_;
    VisualComponent* parentComponent_;
    QColor color_;
    bool isDragging_ = false;
};
