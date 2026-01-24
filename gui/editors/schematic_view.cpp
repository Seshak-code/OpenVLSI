#include "schematic_view.h"
#include <QApplication>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QDebug>

SchematicView::SchematicView(QGraphicsScene* scene, QWidget* parent) 
    : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::RubberBandDrag);
    setFocusPolicy(Qt::StrongFocus);
}

void SchematicView::wheelEvent(QWheelEvent* event) {
    // Zooming with modifier key
    // OLD: macOS: Ctrl, Windows: Alt
    // NEW Phase 11.3: Alt for ALL platforms (User Request)
    
    // Check for Alt modifier primarily, fall back to Ctrl for legacy if needed/desired
    bool zoomModifier = event->modifiers() & Qt::AltModifier; 
    
    // Also support Ctrl for existing muscle memory if user wants, but Alt is the target
    if (!zoomModifier) zoomModifier = event->modifiers() & Qt::ControlModifier;

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

bool SchematicView::viewportEvent(QEvent* event) {
    switch (event->type()) {
        case QEvent::NativeGesture: {
            QNativeGestureEvent* nge = static_cast<QNativeGestureEvent*>(event);
            if (nge->gestureType() == Qt::ZoomNativeGesture) {
                // Determine scale factor from gesture value
                // Value is typically a small delta like +/- 0.05
                double scaleFactor = nge->value(); 
                const double reference = 1.0;
                const double sensitivity = 1.5;
                
                double s = reference + (scaleFactor * sensitivity);
                
                // Safety clamp
                if (s < 0.1) s = 0.1;

                scale(s, s);
                return true;
            }
            break;
        }
        default:
            break;
    }
    return QGraphicsView::viewportEvent(event);
}
