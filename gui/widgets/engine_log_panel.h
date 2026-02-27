#pragma once
// ============================================================================
// engine_log_panel.h — Structured Engine Event Log Viewer
// ============================================================================
// Displays engine telemetry (info/warning/error/convergence) from the
// SimulationDriverAdapter's engineEvent signal.
//
// ARCHITECTURAL RULE: This widget must NEVER include physics, solver, or
// orchestration-internal headers. It consumes EngineEventDTO only.
// ============================================================================

#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVector>

#include "engine_api/engine_event_dto.h"

namespace acutesim {
namespace gui {
namespace widgets {

class EngineLogPanel : public QWidget {
    Q_OBJECT
public:
    explicit EngineLogPanel(QWidget* parent = nullptr);

public slots:
    void appendEvent(const acutesim::compute::orchestration::EngineEventDTO& event);
    void clear();
    void exportLog();

private slots:
    void onFilterChanged(int index);

private:
    void setupUi();
    void applyFilter();

    QTableWidget* table_;
    QComboBox* filterCombo_;
    QPushButton* clearBtn_;
    QPushButton* exportBtn_;

    QVector<acutesim::compute::orchestration::EngineEventDTO> allEvents_;
    int currentFilter_ = -1; // -1 = All
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
