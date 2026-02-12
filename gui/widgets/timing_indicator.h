#pragma once

#include <QWidget>
#include <QPainter>
#include <cmath>

namespace acutesim::gui::widgets {

/**
 * @brief Traffic-light style timing status indicator.
 *
 * Green: timing clean (WNS <= 0, no violations)
 * Yellow: marginal (WNS between -100ps and 0)
 * Red: failing (WNS > 0 or violations > 0)
 */
class TimingIndicatorWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimingIndicatorWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(120, 80);
        setStyleSheet("background-color: #1e1e1e;");
    }

    void setTiming(double wns, double tns, int setupViolations, int holdViolations) {
        m_wns = wns;
        m_tns = tns;
        m_setupViolations = setupViolations;
        m_holdViolations = holdViolations;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Determine status color
        QColor statusColor;
        QString statusText;
        if (m_setupViolations > 0 || m_holdViolations > 0 || m_wns > 0) {
            statusColor = QColor("#F44336"); // Red
            statusText = "FAIL";
        } else if (m_wns > -100.0) {
            statusColor = QColor("#FFC107"); // Yellow
            statusText = "MARGINAL";
        } else {
            statusColor = QColor("#4CAF50"); // Green
            statusText = "CLEAN";
        }

        int w = width();

        // Draw status circle
        int circleR = 14;
        int circleX = w / 2;
        int circleY = 22;
        p.setBrush(statusColor);
        p.setPen(QPen(statusColor.darker(130), 2));
        p.drawEllipse(QPointF(circleX, circleY), circleR, circleR);

        // Status label
        p.setPen(QColor("#fff"));
        p.setFont(QFont("sans-serif", 9, QFont::Bold));
        p.drawText(QRect(0, circleY + circleR + 4, w, 16), Qt::AlignHCenter, statusText);

        // WNS/TNS values
        p.setPen(QColor("#bbb"));
        p.setFont(QFont("monospace", 9));
        int textY = circleY + circleR + 24;
        p.drawText(QRect(0, textY, w, 14), Qt::AlignHCenter,
                   QString("WNS: %1 ps").arg(m_wns, 0, 'f', 1));
        p.drawText(QRect(0, textY + 14, w, 14), Qt::AlignHCenter,
                   QString("TNS: %1 ps").arg(m_tns, 0, 'f', 1));

        if (m_setupViolations > 0 || m_holdViolations > 0) {
            p.setPen(QColor("#F44336"));
            p.drawText(QRect(0, textY + 28, w, 14), Qt::AlignHCenter,
                       QString("Setup: %1  Hold: %2").arg(m_setupViolations).arg(m_holdViolations));
        }
    }

private:
    double m_wns = 0.0;
    double m_tns = 0.0;
    int m_setupViolations = 0;
    int m_holdViolations = 0;
};

} // namespace acutesim::gui::widgets
