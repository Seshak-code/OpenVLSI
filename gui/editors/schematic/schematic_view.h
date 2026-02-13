#pragma once

#include <QGraphicsView>
#include <QWheelEvent>

class SchematicView : public QGraphicsView {
    Q_OBJECT
public:
    explicit SchematicView(QGraphicsScene* scene, QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
    bool viewportEvent(QEvent* event) override;
};
