#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QToolBar>
#include <QAction>
#include <QMenu>
#include <QPointer>

class WaveformView; // Forward decl

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    // Reparenting API (Dock <-> Tab)
    // Called by DockingManager/MainWindow to toggle mode
    void setFloatingMode(bool isFloatingTab);
    bool isFloatingTab() const { return isFloatingTab_; }

    // State Management (Preserve during reparenting)
    void appendDataPoint(double time, double value); // Streaming test hook
    void clear();

    // Accessors for testing
    int dataPointCount() const;

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
    
    // Minimal internal state for this phase
    // In a real app this would likely be a pointer to a shared SimulationRun object
    // to avoid copying massive data. Here we store simple points for verification.
    QVector<QPointF> traceData_; 
};

// Specialized View for Graphics/Gestures
class WaveformView : public QGraphicsView {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);
    void setData(const QVector<QPointF>& data);

signals:
    void zoomChanged(double newScale);

protected:
    // Navigation Gestures (Phase 11.3 Requirement)
    void wheelEvent(QWheelEvent* event) override;
    bool viewportEvent(QEvent* event) override; // For native Pinch gestures

private:
    QGraphicsScene* scene_;
    // Basic rendering item placeholder (in real app, use WebGPU/OpenGL widget)
    QGraphicsPathItem* curveItem_; 
    
    void handlePinch(class QNativeGestureEvent* event);
};
