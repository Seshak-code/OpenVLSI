#pragma once
// ============================================================================
// waveform_data_adapter.h — WaveformTrace → Qt types adapter (GUI side)
// ============================================================================
// Direction is STRICTLY one-way: engine types → Qt types.
// Never pass Qt types upstream toward engine_api or connectivity.
// ============================================================================

#include "../../engine_api/results_db.h"
#include <QVector>
#include <QPointF>
#include <QString>
#include <vector>

namespace acutesim::gui::widgets {

struct QWaveformSeries {
    QString          name;
    QVector<QPointF> points;
};

/// Convert a vector of pure WaveformTrace into Qt-renderable QWaveformSeries.
/// Called by WaveformWidget when ResultsDB finalizes a job.
inline std::vector<QWaveformSeries>
adaptTraces(const std::vector<acutesim::WaveformTrace>& traces)
{
    std::vector<QWaveformSeries> out;
    out.reserve(traces.size());
    for (const auto& trace : traces) {
        QWaveformSeries series;
        series.name = QString::fromStdString(trace.name);
        const std::size_t n = std::min(trace.x.size(), trace.y.size());
        series.points.reserve(static_cast<qsizetype>(n));
        for (std::size_t i = 0; i < n; ++i) {
            series.points.append({trace.x[i], trace.y[i]});
        }
        out.push_back(std::move(series));
    }
    return out;
}

} // namespace acutesim::gui::widgets
