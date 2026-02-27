#include "waveform_widget.h"
#include <QVBoxLayout>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsLineItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QDebug>
#include <QLabel>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include "../../gui/styling/theme_manager.h"

// ============================================================================
// Waveform Widget (Container)
// ============================================================================
namespace acutesim {
namespace gui {
namespace widgets {

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
    toolbar_->addAction("Export CSV", this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Export Waveform", "waveform.csv",
                                                      "CSV Files (*.csv);;All Files (*)");
        if (!path.isEmpty()) exportToCsv(path);
    });

    layout->addWidget(toolbar_);

    // WaveformView - hardware-accelerated waveform rendering
    view_ = new WaveformView(this);
    layout->addWidget(view_);

    // Cursor coordinate label
    cursorLabel_ = new QLabel(this);
    cursorLabel_->setStyleSheet(
        "QLabel { background: rgba(30, 30, 46, 200); color: #cdd6f4; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 2px 6px; }");
    cursorLabel_->setFixedHeight(20);
    cursorLabel_->setText("Cursor: --");
    layout->addWidget(cursorLabel_);

    // Wire cursor signal
    connect(view_, &WaveformView::cursorMoved, this,
            [this](double x, double y, int trace) {
        lastMeasurement_ = {x, y, trace, true};
        QString traceName = traceNames_.value(trace, QString("Trace %1").arg(trace));
        cursorLabel_->setText(QString("X: %1  Y: %2  [%3]")
                              .arg(x, 0, 'g', 6).arg(y, 0, 'g', 6).arg(traceName));
    });
    
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
    traceNames_.clear();
    netTraceMap_.clear();
    batchOverlays_.clear();
    lastMeasurement_ = {};
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

// ── CSV Export ──────────────────────────────────────────────────────────────
bool WaveformWidget::exportToCsv(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);

    QList<int> traceIndices = traceData_.keys();
    std::sort(traceIndices.begin(), traceIndices.end());

    // Header
    out << "x";
    for (int idx : traceIndices) {
        out << "," << traceNames_.value(idx, QString("trace_%1").arg(idx));
    }
    out << "\n";

    int maxLen = 0;
    for (const auto& trace : traceData_) {
        maxLen = std::max(maxLen, static_cast<int>(trace.size()));
    }

    for (int row = 0; row < maxLen; ++row) {
        double xVal = 0.0;
        if (!traceIndices.isEmpty() && row < traceData_[traceIndices[0]].size()) {
            xVal = traceData_[traceIndices[0]][row].x();
        }
        out << QString::number(xVal, 'g', 12);

        for (int idx : traceIndices) {
            out << ",";
            if (row < traceData_[idx].size()) {
                out << QString::number(traceData_[idx][row].y(), 'g', 12);
            }
        }
        out << "\n";
    }
    return true;
}

// ── Batch Overlay ───────────────────────────────────────────────────────────
void WaveformWidget::addBatchOverlay(int batchIndex, const QMap<int, QVector<QPointF>>& traces) {
    batchOverlays_[batchIndex] = traces;
    if (view_) {
        for (auto it = traces.begin(); it != traces.end(); ++it) {
            int overlayIndex = 1000 * batchIndex + it.key();
            view_->setData(it.value(), overlayIndex);
        }
    }
}

void WaveformWidget::clearBatchOverlays() {
    batchOverlays_.clear();
    if (view_) {
        view_->clearAll();
        view_->setMultiTraceData(traceData_);
    }
}

// ============================================================================
// Trace Management & Observer
// ============================================================================

void WaveformWidget::setTraceName(int traceIndex, const QString& name) {
    traceNames_[traceIndex] = name;
    qDebug() << "[WaveformWidget] Trace " << traceIndex << " renamed to " << name;
}

void WaveformWidget::registerTrace(int traceIndex, acutesim::components::NetUUID netId, const QString& name) {
    netTraceMap_[netId] = traceIndex;
    traceNames_[traceIndex] = name;
}

void WaveformWidget::onNetRenamed(acutesim::components::NetUUID id, const std::string& oldName, const std::string& newName) {
    Q_UNUSED(oldName);
    if (netTraceMap_.contains(id)) {
        int traceIndex = netTraceMap_[id];
        setTraceName(traceIndex, QString::fromStdString(newName));
    }
}

// ============================================================================
// WaveformView (Graphics View)
// ============================================================================

WaveformView::WaveformView(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);

    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::RubberBandDrag);
    setMouseTracking(true);

    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    setBackgroundBrush(tm.getColor("schematic.background"));

    // Cursor crosshair
    QPen cursorPen(QColor(166, 173, 200, 120), 1);
    cursorPen.setCosmetic(true);
    cursorPen.setStyle(Qt::DashLine);
    cursorVLine_ = scene_->addLine(0, -10000, 0, 10000, cursorPen);
    cursorHLine_ = scene_->addLine(-10000, 0, 10000, 0, cursorPen);
    cursorVLine_->setZValue(100);
    cursorHLine_->setZValue(100);
}

QColor WaveformView::getTraceColor(int traceIndex) const {
    static const QVector<QColor> colors = {
        QColor(0, 255, 255),    QColor(255, 255, 0),
        QColor(255, 0, 255),    QColor(0, 255, 0),
        QColor(255, 128, 0),    QColor(128, 128, 255),
        QColor(255, 128, 128),  QColor(128, 255, 128),
    };
    return colors[traceIndex % colors.size()];
}

void WaveformView::setData(const QVector<QPointF>& data, int traceIndex) {
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

    double xMin = data.first().x(), xMax = data.first().x();
    double yMin = data.first().y(), yMax = data.first().y();
    for (const auto& pt : data) {
        if (pt.x() < xMin) xMin = pt.x();
        if (pt.x() > xMax) xMax = pt.x();
        if (pt.y() < yMin) yMin = pt.y();
        if (pt.y() > yMax) yMax = pt.y();
    }

    double xRange = (xMax - xMin);
    double yRange = (yMax - yMin);
    if (xRange < 1e-12) xRange = 1.0;
    if (yRange < 1e-12) yRange = 1.0;

    double xScale = 800.0 / xRange;
    double yScale = 300.0 / yRange;

    QPainterPath path;
    path.moveTo((data.first().x() - xMin) * xScale, -(data.first().y() - yMin) * yScale);
    for (int i = 1; i < data.size(); ++i) {
        path.lineTo((data[i].x() - xMin) * xScale, -(data[i].y() - yMin) * yScale);
    }
    curveItem->setPath(path);

    if (data.size() > 10) {
        fitInView(scene_->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    }
}

void WaveformView::setMultiTraceData(const QMap<int, QVector<QPointF>>& traces) {
    for (auto it = traces.begin(); it != traces.end(); ++it) {
        setData(it.value(), it.key());
    }

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

// ── Cursor Crosshair ────────────────────────────────────────────────────────

void WaveformView::mouseMoveEvent(QMouseEvent* event) {
    if (cursorEnabled_ && cursorVLine_ && cursorHLine_) {
        QPointF scenePos = mapToScene(event->pos());
        cursorVLine_->setLine(scenePos.x(), scenePos.y() - 5000, scenePos.x(), scenePos.y() + 5000);
        cursorHLine_->setLine(scenePos.x() - 5000, scenePos.y(), scenePos.x() + 5000, scenePos.y());

        double bestDist = 1e18;
        double bestY = 0.0;
        int bestTrace = 0;

        for (auto it = curveItems_.begin(); it != curveItems_.end(); ++it) {
            QPainterPath path = it.value()->path();
            double minDx = 1e18;
            for (int i = 0; i < path.elementCount(); ++i) {
                auto elem = path.elementAt(i);
                double dx = std::abs(elem.x - scenePos.x());
                if (dx < minDx) {
                    minDx = dx;
                    if (dx < bestDist) {
                        bestDist = dx;
                        bestY = elem.y;
                        bestTrace = it.key();
                    }
                }
            }
        }

        emit cursorMoved(scenePos.x(), scenePos.y(), bestTrace);
    }
    QGraphicsView::mouseMoveEvent(event);
}

// ── Navigation Gestures ─────────────────────────────────────────────────────

void WaveformView::wheelEvent(QWheelEvent* event) {
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
    double scaleFactor = event->value();
    double s = 1.0 + scaleFactor;
    if (s < 0.1) s = 0.1; 
    scale(s, s);
    emit zoomChanged(transform().m11());
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
