#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QToolBar>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QNativeGestureEvent>
#include "../../components/design_db.h"

namespace acutesim {
namespace gui {
namespace widgets {

class WaveformView; // Forward decl

class WaveformWidget : public QWidget, public acutesim::components::DesignDbObserver {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    // Reparenting API (Dock <-> Tab)
    // Called by DockingManager/MainWindow to toggle mode
    void setFloatingMode(bool isFloatingTab);
    bool isFloatingTab() const { return isFloatingTab_; }

    // State Management (Preserve during reparenting)
    void appendDataPoint(double time, double value, int traceIndex = 0); // Multi-trace support
    void appendDataPoints(double time, const QVector<double>& values);    // Batch add all traces

    void clear();
    
    // Trace Registration (Crucial for Tcl Sync Optimization)
    void registerTrace(int traceIndex, acutesim::components::NetUUID netId, const QString& name);
    
    // Trace Management
    void setTraceName(int traceIndex, const QString& name);
    
    // DesignDbObserver Interface
    void onNetRenamed(acutesim::components::NetUUID id, const std::string& oldName, const std::string& newName) override;

    // Accessors for testing
    int dataPointCount() const;
    int traceCount() const { return traceData_.size(); }

signals:
    void promoteToTabRequested();
    void returnToDockRequested();

private slots:
    void onToggleViewMode();

private:
    WaveformView* view_;
    QToolBar* toolbar_;
    QAction* toggleModeAction_;
    bool isFloatingTab_ = false; // False = Docked, True = Central Tab
    
    // Multi-trace data storage
    // Key = trace index (node number), Value = time-value pairs
    QMap<int, QVector<QPointF>> traceData_;
    
    // Mapping for Renaming
    // We need to map NetUUID -> traceIndex to know which trace to rename.
    QMap<acutesim::components::NetUUID, int> netTraceMap_; // Optimized Lookup
    QMap<int, QString> traceNames_; // Store names for legend/tooltip
};

// Specialized View for Graphics/Gestures
class WaveformView : public QGraphicsView {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);
    void setData(const QVector<QPointF>& data, int traceIndex = 0);
    void setMultiTraceData(const QMap<int, QVector<QPointF>>& traces);
    void clearAll();

signals:
    void zoomChanged(double newScale);

protected:
    // Navigation Gestures (Phase 11.3 Requirement)
    void wheelEvent(QWheelEvent* event) override;
    bool viewportEvent(QEvent* event) override; // For native Pinch gestures

private:
    QGraphicsScene* scene_;
    // Multi-trace support: one curve item per trace, different colors
    QMap<int, QGraphicsPathItem*> curveItems_;

    QColor getTraceColor(int traceIndex) const;
    void handlePinch(class QNativeGestureEvent* event);
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
