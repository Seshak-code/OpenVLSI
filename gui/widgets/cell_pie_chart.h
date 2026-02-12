#pragma once

#include <QWidget>
#include <QPainter>
#include <QMap>
#include <cmath>

namespace acutesim::gui::widgets {

/**
 * @brief QPainter-based pie chart showing cell type breakdown from synthesis.
 *
 * Follows the same pattern as YieldHistogramWidget — no Qt Charts dependency.
 */
class CellPieChartWidget : public QWidget {
    Q_OBJECT

public:
    explicit CellPieChartWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(250, 200);
        setStyleSheet("background-color: #1e1e1e;");
    }

    void setData(const QMap<QString, int>& cellBreakdown) {
        m_data = cellBreakdown;
        m_total = 0;
        for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it)
            m_total += it.value();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (m_data.isEmpty() || m_total == 0) {
            p.setPen(QColor("#888"));
            p.drawText(rect(), Qt::AlignCenter, "No cell data");
            return;
        }

        int w = width();
        int h = height();
        int legendWidth = w / 3;
        int chartSize = std::min(w - legendWidth - 20, h - 20);
        int cx = (w - legendWidth) / 2;
        int cy = h / 2;
        QRect pieRect(cx - chartSize / 2, cy - chartSize / 2, chartSize, chartSize);

        // Draw pie slices
        static const QColor palette[] = {
            QColor("#4CAF50"), QColor("#2196F3"), QColor("#FF9800"),
            QColor("#E91E63"), QColor("#9C27B0"), QColor("#00BCD4"),
            QColor("#FF5722"), QColor("#607D8B")
        };
        int paletteSize = 8;

        double startAngle = 0.0;
        int colorIdx = 0;
        QVector<QPair<QString, QColor>> legend;

        for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
            double fraction = static_cast<double>(it.value()) / m_total;
            double spanAngle = fraction * 360.0 * 16.0; // Qt uses 1/16th degree

            QColor color = palette[colorIdx % paletteSize];
            p.setBrush(color);
            p.setPen(QPen(QColor("#1e1e1e"), 1));
            p.drawPie(pieRect, static_cast<int>(startAngle), static_cast<int>(spanAngle));

            legend.append({it.key(), color});
            startAngle += spanAngle;
            colorIdx++;
        }

        // Draw legend
        int legendX = w - legendWidth + 5;
        int legendY = 15;
        p.setFont(QFont("monospace", 9));

        for (const auto& item : legend) {
            if (legendY + 14 > h) break;

            p.setBrush(item.second);
            p.setPen(Qt::NoPen);
            p.drawRect(legendX, legendY, 10, 10);

            int count = m_data.value(item.first);
            double pct = 100.0 * count / m_total;
            p.setPen(QColor("#ccc"));
            p.drawText(legendX + 14, legendY + 10,
                       QString("%1 (%2%)").arg(item.first).arg(pct, 0, 'f', 1));
            legendY += 16;
        }
    }

private:
    QMap<QString, int> m_data;
    int m_total = 0;
};

} // namespace acutesim::gui::widgets
