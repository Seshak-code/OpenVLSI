#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVector>
#include <QMap>
#include <QPainter>
#include "circuit.h"

// ============================================================================
// Logic Analyzer UI (Phase 13.2)
// Visualizes digital logic channels and buses in a "VS Code" style dark theme.
// ============================================================================

struct WaveformTrack {
    QString name;
    bool isBus;
    int bitWidth;
    
    // Data References (Points to simulation history)
    // We store copies here for rendering performance to separate UI from Sim thread
    struct Sample {
        double time;
        uint64_t value; // 0 or 1 for scalar, value for bus
        bool isUnknown; // 'X' state
        bool isHiZ;     // 'Z' state
    };
    QVector<Sample> history;
    
    // Computed render properties
    double yOffset;
    double height;
};

class LogicWaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogicWaveformWidget(QWidget *parent = nullptr);
    
    // Data Ingestion
    void clear();
    void addBusData(const std::string& name, const std::vector<BusProbe::DigitalSample>& history, int bitWidth);
    
    // View Controls
    void setTimeRange(double maxTime); // Updates horizontal scroll scale
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override; // for Zoom
    void mouseMoveEvent(QMouseEvent *event) override; // for Cursor

private:
    void drawGrid(QPainter& p);
    void drawScalarTrack(QPainter& p, const WaveformTrack& track, int trackIdx);
    void drawBusTrack(QPainter& p, const WaveformTrack& track, int trackIdx);
    
    // Theme Constants (VS Code / Dark Mode)
    const QColor COL_BG = QColor(30, 30, 30);      // #1e1e1e
    const QColor COL_GRID = QColor(60, 60, 60);    // #3c3c3c
    const QColor COL_TEXT = QColor(200, 200, 200); // #c8c8c8
    
    const QColor COL_HIGH = QColor(76, 175, 80);   // #4CAF50 (Green)
    const QColor COL_LOW = QColor(100, 100, 100);  // #646464 (Gray)
    const QColor COL_BUS = QColor(33, 150, 243);   // #2196F3 (Blue)
    const QColor COL_X = QColor(244, 67, 54);      // #F44336 (Red)
    
    // Layout Metrics
    const int TRACK_HEIGHT = 40;
    const int TRACK_SPACING = 10;
    const int LEFT_MARGIN = 120; // For Labels
    
    // State
    QVector<WaveformTrack> tracks;
    double maxTime = 0.002; // 2ms default
    double pixelsPerSecond = 100000.0; // Zoom level
    double scrollX = 0;
    
    // Cursor
    double cursorTime = -1.0;
    int mouseX = 0;
};
