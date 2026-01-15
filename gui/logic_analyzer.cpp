#include "logic_analyzer.h"
#include <QPainterPath>
#include <QResizeEvent>
#include <QMouseEvent>
#include <cmath>

// ============================================================================
// Logic Analyzer Implementation
// ============================================================================

LogicWaveformWidget::LogicWaveformWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true); // For cursor
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    
    // Set default dark palette for this widget
    QPalette pal = palette();
    pal.setColor(QPalette::Base, COL_BG);
    pal.setColor(QPalette::Window, COL_BG);
    setPalette(pal);
    
    // Default nice zoom
    pixelsPerSecond = 50000.0; // 50px per ms
}

void LogicWaveformWidget::clear() {
    tracks.clear();
    update();
}

void LogicWaveformWidget::addBusData(const std::string& name, const std::vector<BusProbe::DigitalSample>& history, int bitWidth) {
    WaveformTrack track;
    track.name = QString::fromStdString(name);
    track.isBus = (bitWidth > 1);
    track.bitWidth = bitWidth;
    
    // Convert history
    for (const auto& h : history) {
        WaveformTrack::Sample s;
        s.time = h.time;
        s.value = h.value;
        s.isUnknown = false; // Simplified for now
        s.isHiZ = false;
        track.history.append(s);
    }
    
    tracks.append(track);
    update();
}

void LogicWaveformWidget::setTimeRange(double maxT) {
    maxTime = maxT;
    update();
}

// ============================================================================
// EVENTS
// ============================================================================

void LogicWaveformWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}

void LogicWaveformWidget::wheelEvent(QWheelEvent *event) {
    // Zoom in/out at mouse cursor
    double zoomFactor = 1.1;
    if (event->angleDelta().y() < 0) zoomFactor = 1.0 / 1.1;
    
    // Clamp zoom
    double newScale = pixelsPerSecond * zoomFactor;
    if (newScale < 100.0) newScale = 100.0;       // Min zoom (compressed)
    if (newScale > 10000000.0) newScale = 10000000.0; // Max zoom (nanoseconds)
    
    // Adjust scroll to keep mouse stable (simple version: center zoom)
    pixelsPerSecond = newScale;
    update();
}

void LogicWaveformWidget::mouseMoveEvent(QMouseEvent *event) {
    mouseX = event->x();
    
    // Update Time Cursor
    if (mouseX > LEFT_MARGIN) {
        double relativeX = mouseX - LEFT_MARGIN + scrollX;
        cursorTime = relativeX / pixelsPerSecond;
    } else {
        cursorTime = -1.0;
    }
    
    update();
}

// ============================================================================
// PAINTING
// ============================================================================

void LogicWaveformWidget::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false); // Crisp lines for logic
    
    // 1. Draw Grid
    drawGrid(p);
    
    // 2. Draw Tracks
    int currentY = 40; // Top margin
    
    for (int i = 0; i < tracks.size(); ++i) {
        const WaveformTrack& track = tracks[i];
        
        // Draw Label Background
        p.fillRect(0, currentY, LEFT_MARGIN, TRACK_HEIGHT, COL_BG); // Cover grid
        p.setPen(COL_TEXT);
        p.drawText(10, currentY + TRACK_HEIGHT/2 + 5, track.name);
        
        // Draw Signal
        p.translate(0, currentY); // Local coordinate system for track
        
        if (track.isBus) {
            drawBusTrack(p, track, i);
        } else {
            drawScalarTrack(p, track, i);
        }
        
        p.translate(0, -currentY); // Restore
        currentY += TRACK_HEIGHT + TRACK_SPACING;
    }
    
    // 3. Draw Cursor Line (if valid)
    if (cursorTime >= 0) {
        int x = LEFT_MARGIN + (cursorTime * pixelsPerSecond) - scrollX;
        if (x >= LEFT_MARGIN && x < width()) {
            p.setPen(QPen(Qt::yellow, 1, Qt::DashLine));
            p.drawLine(x, 0, x, height());
            
            // Time Label Tooltip
            QString timeStr = QString("%1 ms").arg(cursorTime * 1000.0, 0, 'f', 4);
            int textW = p.fontMetrics().horizontalAdvance(timeStr) + 10;
            p.fillRect(x + 5, 5, textW, 20, QColor(0, 0, 0, 180));
            p.setPen(Qt::white);
            p.drawText(x + 10, 20, timeStr);
        }
    }
}

void LogicWaveformWidget::drawGrid(QPainter& p) {
    p.setPen(QPen(COL_GRID, 1, Qt::DotLine));
    
    // Vertical Grid Lines (Time)
    // Dynamic grid step based on zoom
    double timeStep = 0.0001; // 100us default
    
    // auto-scale grid (1ms, 100us, 10us, 1us...)
    double pxStep = timeStep * pixelsPerSecond;
    while (pxStep < 50) { timeStep *= 2; pxStep = timeStep * pixelsPerSecond; }
    while (pxStep > 150) { timeStep /= 2; pxStep = timeStep * pixelsPerSecond; }
    
    for (double t = 0; t <= maxTime; t += timeStep) {
        int x = LEFT_MARGIN + (int)(t * pixelsPerSecond) - scrollX;
        if (x > width()) break;
        if (x < LEFT_MARGIN) continue;
        
        p.drawLine(x, 0, x, height());
        
        // Time Label at top
        p.drawText(x + 2, 15, QString("%1u").arg(t * 1e6, 0, 'f', 0));
    }
    
    // Left sidebar separator
    p.setPen(QPen(Qt::gray, 1));
    p.drawLine(LEFT_MARGIN, 0, LEFT_MARGIN, height());
}

void LogicWaveformWidget::drawScalarTrack(QPainter& p, const WaveformTrack& track, int trackIdx) {
    if (track.history.empty()) return;

    double yHi = 5.0;            // Top of track
    double yLo = TRACK_HEIGHT - 5.0; // Bottom of track
    double mid = (yHi + yLo) / 2;
    
    QPainterPath path;
    bool first = true;
    double lastX = LEFT_MARGIN;
    uint64_t lastVal = 0;
    
    p.setPen(QPen(COL_HIGH, 2));
    
    for (const auto& s : track.history) {
        double x = LEFT_MARGIN + (s.time * pixelsPerSecond) - scrollX;
        
        // Optimization: Don't draw if off-screen left
        if (x < LEFT_MARGIN) { 
            lastX = x; lastVal = s.value; first = false; continue; 
        }
        // Stop if off-screen right
        if (x > width()) break;
        
        if (first) {
            path.moveTo(x, (s.value ? yHi : yLo));
            first = false;
        } else {
            // Draw transition
            double prevY = (lastVal ? yHi : yLo);
            double currY = (s.value ? yHi : yLo);
            
            path.lineTo(x, prevY); // Hold previous value until new time
            path.lineTo(x, currY); // Transition edge
        }
        
        lastX = x;
        lastVal = s.value;
    }
    
    // Extend to end
    double endX = LEFT_MARGIN + (maxTime * pixelsPerSecond) - scrollX;
    if (endX > width()) endX = width();
    path.lineTo(endX, (lastVal ? yHi : yLo));
    
    p.drawPath(path);
}

void LogicWaveformWidget::drawBusTrack(QPainter& p, const WaveformTrack& track, int trackIdx) {
    if (track.history.empty()) return;
    
    double yHi = 5.0;
    double yLo = TRACK_HEIGHT - 5.0;
    double mid = (yHi + yLo) / 2;
    
    p.setPen(QPen(COL_BUS, 1));
    p.setBrush(Qt::NoBrush);
    
    // Render "Hex Capsules"
    // <XXXX>----<YYYY>
    
    for (int i = 0; i < track.history.size(); ++i) {
        const auto& s = track.history[i];
        double startX = LEFT_MARGIN + (s.time * pixelsPerSecond) - scrollX;
        
        // Find end time (next sample or maxTime)
        double endTime = maxTime;
        if (i + 1 < track.history.size()) {
            endTime = track.history[i+1].time;
        }
        double endX = LEFT_MARGIN + (endTime * pixelsPerSecond) - scrollX;
        
        // Visibility Check
        if (endX < LEFT_MARGIN) continue;
        if (startX > width()) break;
        
        // Clip visual range
        double visStart = std::max((double)LEFT_MARGIN, startX);
        double visEnd = std::min((double)width(), endX);
        
        if (visEnd - visStart < 1) continue;
        
        // Draw Capsule
        //  /----\
        // < HEX  >
        //  \----/
        
        // Hex text config
        QString hexText = QString("0x%1").arg(s.value, (track.bitWidth+3)/4, 16, QChar('0')).toUpper();
        
        // Change color based on changes? Or just static blue
        QPointF points[6] = {
            QPointF(startX, mid),
            QPointF(startX + 5, yHi),
            QPointF(endX - 5, yHi),
            QPointF(endX, mid),
            QPointF(endX - 5, yLo),
            QPointF(startX + 5, yLo)
        };
        
        if (endX - startX < 10) {
            // Too small for transition arrows, just draw rect
            p.drawRect(startX, yHi, endX-startX, yLo-yHi);
        } else {
            p.drawPolygon(points, 6);
            
            // Draw Text if space permits
            if (visEnd - visStart > 30) {
                p.setPen(COL_TEXT);
                // Center text in visible portion for usability
                double textCenter = (visStart + visEnd) / 2;
                p.drawText(textCenter - 20, mid + 5, hexText);
                p.setPen(COL_BUS); // Restore
            }
        }
    }
}
