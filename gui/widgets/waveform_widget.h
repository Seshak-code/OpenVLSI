#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QToolBar>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QNativeGestureEvent>
#include <QLabel>
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

    // Cursor measurement
    struct CursorMeasurement {
        double x = 0.0;
        double y = 0.0;
        int traceIndex = 0;
        bool valid = false;
    };
    CursorMeasurement lastCursorMeasurement() const { return lastMeasurement_; }

    // CSV Export
    bool exportToCsv(const QString& path) const;

    // Batch overlay: load multiple response trace sets
    void addBatchOverlay(int batchIndex, const QMap<int, QVector<QPointF>>& traces);
    void clearBatchOverlays();

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
    QMap<acutesim::components::NetUUID, int> netTraceMap_; // Optimized Lookup
    QMap<int, QString> traceNames_; // Store names for legend/tooltip

    // Cursor
    CursorMeasurement lastMeasurement_;
    QLabel* cursorLabel_ = nullptr;

    // Batch overlays: batchIndex -> (traceIndex -> data)
    QMap<int, QMap<int, QVector<QPointF>>> batchOverlays_;
};

// Specialized View for Graphics/Gestures
class WaveformView : public QGraphicsView {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);
    void setData(const QVector<QPointF>& data, int traceIndex = 0);
    void setMultiTraceData(const QMap<int, QVector<QPointF>>& traces);
    void clearAll();

    // Cursor crosshair
    void setCursorEnabled(bool enabled) { cursorEnabled_ = enabled; }

signals:
    void zoomChanged(double newScale);
    void cursorMoved(double x, double y, int nearestTrace);

protected:
    void wheelEvent(QWheelEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QGraphicsScene* scene_;
    // Multi-trace support: one curve item per trace, different colors
    QMap<int, QGraphicsPathItem*> curveItems_;

    QColor getTraceColor(int traceIndex) const;
    void handlePinch(class QNativeGestureEvent* event);

    // Cursor
    bool cursorEnabled_ = true;
    QGraphicsLineItem* cursorVLine_ = nullptr;
    QGraphicsLineItem* cursorHLine_ = nullptr;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
