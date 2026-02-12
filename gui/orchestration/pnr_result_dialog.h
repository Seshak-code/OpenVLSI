#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "compute/orchestration/drivers/openroad_report_parser.h"
#include "gui/widgets/timing_indicator.h"

namespace acutesim::gui::orchestration {

/**
 * @brief Modeless dialog showing structured PnR results.
 *
 * Tabs: Placement, Timing, Routing, Power, Log.
 */
class PnRResultDialog : public QDialog {
    Q_OBJECT

public:
    explicit PnRResultDialog(const acutesim::compute::orchestration::PnRResult& result,
                              QWidget* parent = nullptr);
};

} // namespace acutesim::gui::orchestration
