#include "schematic_view.h"
#include <QApplication>

SchematicView::SchematicView(QGraphicsScene* scene, QWidget* parent) 
    : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::RubberBandDrag);
}

void SchematicView::wheelEvent(QWheelEvent* event) {
    // Zooming with modifier key
    // macOS: Ctrl, Windows: Alt
#ifdef Q_OS_MAC
    bool zoomModifier = event->modifiers() & Qt::ControlModifier;
#else
    bool zoomModifier = event->modifiers() & Qt::AltModifier;
#endif

    if (zoomModifier) {
        double scaleFactor = 1.15;
        if (event->angleDelta().y() < 0) {
            scaleFactor = 1.0 / scaleFactor;
        }

        // Apply scale with bounds (10% to 1000%)
        double currentScale = transform().m11();
        double nextScale = currentScale * scaleFactor;
        
        if (nextScale > 0.1 && nextScale < 10.0) {
            scale(scaleFactor, scaleFactor);
        }
    } else {
        QGraphicsView::wheelEvent(event);
    }
}
