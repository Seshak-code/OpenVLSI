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

    // View (Placeholder to prevent WASM crash)
    QLabel* placeholder = new QLabel("Waveform View Loading...", this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("color: #888;");
    layout->addWidget(placeholder);
    
    // view_ = new WaveformView(this);
    // layout->addWidget(view_);
    
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

void WaveformWidget::appendDataPoint(double time, double value) {
    traceData_.append(QPointF(time, value));
    view_->setData(traceData_);
}

void WaveformWidget::clear() {
    traceData_.clear();
    view_->setData(traceData_);
}

int WaveformWidget::dataPointCount() const {
    return traceData_.size();
}

// ============================================================================
// Waveform View (Graphics & Gestures)
// ============================================================================
WaveformView::WaveformView(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    
    curveItem_ = new QGraphicsPathItem();
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    QPen pen(tm.getColor("schematic.wire"));
    // Phase 11.4: Strict Cosmetic Contract
    // Width 0 means 1 physical pixel on device (ignores zoom)
    // Width 1 means 1 logical unit (scales with zoom)
    // For waveforms, we want them visible but crisp. Usually width 2 logical is good, 
    // but on high DPI we might want width 1.5 logical.
    // Cosmetic=true means width is in device pixels/independent of zoom.
    // Let's stick to Cosmetic=true, width 2 for clear visibility.
    pen.setWidth(2);
    pen.setCosmetic(true); 
    curveItem_->setPen(pen);
    scene_->addItem(curveItem_);
    
    // Optimization
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::RubberBandDrag); // Or ScrollHandDrag
    
    // Set Background
    setBackgroundBrush(tm.getColor("schematic.background"));
}

void WaveformView::setData(const QVector<QPointF>& data) {
    if (data.isEmpty()) {
        curveItem_->setPath(QPainterPath());
        return;
    }
    
    // Simple path construction (optimize later for massive datasets)
    QPainterPath path;
    // Scale X slightly to visualize check
    double xScale = 10.0; 
    path.moveTo(data.first().x() * xScale, -data.first().y()); 
    for (int i = 1; i < data.size(); ++i) {
        path.lineTo(data[i].x() * xScale, -data[i].y());
    }
    curveItem_->setPath(path);
    // Optionally fit in view if creating fresh? 
    // scene_->setSceneRect(path.boundingRect());
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
