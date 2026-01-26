#include "waveform_widget.h"
#include <QVBoxLayout>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QDebug>
#include <QLabel>
#include "../../gui/styling/theme_manager.h"

// ============================================================================
// Waveform Widget (Container)
// ============================================================================
WaveformWidget::WaveformWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    toolbar_ = new QToolBar(this);
    toolbar_->setIconSize(QSize(16, 16));
    
    toggleModeAction_ = toolbar_->addAction("Promote to Tab");
    toggleModeAction_->setIcon(QIcon::fromTheme("view-fullscreen")); 
    connect(toggleModeAction_, &QAction::triggered, this, &WaveformWidget::onToggleViewMode);
    
    toolbar_->addSeparator();
    toolbar_->addAction("Clear", this, &WaveformWidget::clear);

    layout->addWidget(toolbar_);

    // WaveformView - hardware-accelerated waveform rendering
    view_ = new WaveformView(this);
    layout->addWidget(view_);
    
    // Style
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    toolbar_->setStyleSheet(QString("QToolBar { background: %1; border-bottom: 1px solid %2; }")
                            .arg(tm.getColor("ui.header").name())
                            .arg(tm.getColor("ui.border").name()));
}

void WaveformWidget::setFloatingMode(bool isFloatingTab) {
    isFloatingTab_ = isFloatingTab;
    if (isFloatingTab_) {
        toggleModeAction_->setText("Return to Dock");
        toggleModeAction_->setToolTip("Move this view back to the bottom dock");
    } else {
        toggleModeAction_->setText("Promote to Tab");
        toggleModeAction_->setToolTip("Move this view to the central editor area");
    }
}

void WaveformWidget::onToggleViewMode() {
    if (isFloatingTab_) {
        emit returnToDockRequested();
    } else {
        emit promoteToTabRequested();
    }
}

void WaveformWidget::appendDataPoint(double time, double value, int traceIndex) {
    traceData_[traceIndex].append(QPointF(time, value));
    if (view_) {
        view_->setData(traceData_[traceIndex], traceIndex);
    }
}

void WaveformWidget::appendDataPoints(double time, const QVector<double>& values) {
    for (int i = 0; i < values.size(); ++i) {
        traceData_[i].append(QPointF(time, values[i]));
    }
    if (view_) {
        view_->setMultiTraceData(traceData_);
    }
}

void WaveformWidget::clear() {
    traceData_.clear();
    if (view_) {
        view_->clearAll();
    }
}

int WaveformWidget::dataPointCount() const {
    int total = 0;
    for (const auto& trace : traceData_) {
        total += trace.size();
    }
    return total;
}

// ============================================================================
// Waveform View (Graphics & Gestures)
// ============================================================================
WaveformView::WaveformView(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);

    // Optimization
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::RubberBandDrag);

    // Set Background
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    setBackgroundBrush(tm.getColor("schematic.background"));
}

QColor WaveformView::getTraceColor(int traceIndex) const {
    // Color palette for multiple traces (oscilloscope-style)
    static const QVector<QColor> colors = {
        QColor(0, 255, 255),    // Cyan - Node 0 (Ground reference typically)
        QColor(255, 255, 0),    // Yellow - Node 1
        QColor(255, 0, 255),    // Magenta - Node 2
        QColor(0, 255, 0),      // Green - Node 3
        QColor(255, 128, 0),    // Orange - Node 4
        QColor(128, 128, 255),  // Light Blue - Node 5
        QColor(255, 128, 128),  // Pink - Node 6
        QColor(128, 255, 128),  // Light Green - Node 7
    };
    return colors[traceIndex % colors.size()];
}

void WaveformView::setData(const QVector<QPointF>& data, int traceIndex) {
    // Create curve item if it doesn't exist
    if (!curveItems_.contains(traceIndex)) {
        QGraphicsPathItem* item = new QGraphicsPathItem();
        QPen pen(getTraceColor(traceIndex));
        pen.setWidth(2);
        pen.setCosmetic(true);
        item->setPen(pen);
        scene_->addItem(item);
        curveItems_[traceIndex] = item;
    }

    QGraphicsPathItem* curveItem = curveItems_[traceIndex];

    if (data.isEmpty()) {
        curveItem->setPath(QPainterPath());
        return;
    }

    // Calculate data bounds for proper scaling
    double xMin = data.first().x(), xMax = data.first().x();
    double yMin = data.first().y(), yMax = data.first().y();
    for (const auto& pt : data) {
        if (pt.x() < xMin) xMin = pt.x();
        if (pt.x() > xMax) xMax = pt.x();
        if (pt.y() < yMin) yMin = pt.y();
        if (pt.y() > yMax) yMax = pt.y();
    }

    // Normalize to scene coordinates (use viewport width/height as reference)
    double xRange = (xMax - xMin);
    double yRange = (yMax - yMin);
    if (xRange < 1e-12) xRange = 1.0;
    if (yRange < 1e-12) yRange = 1.0;

    // Scale to fit in a reasonable scene area (e.g., 800x400)
    double xScale = 800.0 / xRange;
    double yScale = 300.0 / yRange;

    // Build path with proper scaling
    QPainterPath path;
    path.moveTo((data.first().x() - xMin) * xScale, -(data.first().y() - yMin) * yScale);
    for (int i = 1; i < data.size(); ++i) {
        path.lineTo((data[i].x() - xMin) * xScale, -(data[i].y() - yMin) * yScale);
    }
    curveItem->setPath(path);

    // Auto-fit view to scene bounds (only after sufficient data)
    if (data.size() > 10) {
        fitInView(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    }
}

void WaveformView::setMultiTraceData(const QMap<int, QVector<QPointF>>& traces) {
    for (auto it = traces.begin(); it != traces.end(); ++it) {
        setData(it.value(), it.key());
    }

    // Auto-fit view after updating all traces
    if (!traces.isEmpty() && scene_->items().count() > 0) {
        QRectF bounds = scene_->itemsBoundingRect();
        if (bounds.isValid() && bounds.width() > 1 && bounds.height() > 1) {
            fitInView(bounds.adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
        }
    }
}

void WaveformView::clearAll() {
    for (auto* item : curveItems_) {
        item->setPath(QPainterPath());
    }
}

// ----------------------------------------------------------------------------
// Navigation Gestures Implementation
// ----------------------------------------------------------------------------

void WaveformView::wheelEvent(QWheelEvent* event) {
    // Requirements:
    // 1. Alt + Scroll = Zoom
    // 2. Normal Scroll = Pan (Default behavior mostly, or handled here)
    // 3. Ctrl + Scroll (Standard Qt) - we can keep or override. 
    
    bool altModifier = event->modifiers() & Qt::AltModifier;
    
    if (altModifier) {
        const double scaleFactor = 1.15;
        if (event->angleDelta().y() > 0) {
            scale(scaleFactor, scaleFactor);
        } else {
            scale(1.0 / scaleFactor, 1.0 / scaleFactor);
        }
        event->accept();
        emit zoomChanged(transform().m11());
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

bool WaveformView::viewportEvent(QEvent* event) {
    switch (event->type()) {
        case QEvent::NativeGesture: {
            QNativeGestureEvent* nge = static_cast<QNativeGestureEvent*>(event);
            if (nge->gestureType() == Qt::ZoomNativeGesture) {
                handlePinch(nge);
                return true;
            }
            break;
        }
        default:
            break;
    }
    return QGraphicsView::viewportEvent(event);
}

void WaveformView::handlePinch(QNativeGestureEvent* event) {
    double scaleFactor = event->value(); // Value is delta, e.g. 0.05
    // Typical pinch logic: 1.0 + value
    // value is positive for zoom in, negative for zoom out
    
    const double reference = 1.0;
    const double sensitivity = 1.0; 
    
    double s = reference + (scaleFactor * sensitivity);
    
    // Safety bounds
    if (s < 0.1) s = 0.1; 
    
    scale(s, s);
    emit zoomChanged(transform().m11());
}
