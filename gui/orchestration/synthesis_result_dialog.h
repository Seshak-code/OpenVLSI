#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "compute/orchestration/drivers/yosys_log_parser.h"
#include "gui/widgets/cell_pie_chart.h"

namespace acutesim::gui::orchestration {

/**
 * @brief Modeless dialog showing structured synthesis results.
 *
 * Tabs: Summary + Cell Breakdown, Module Stats, Log.
 */
class SynthesisResultDialog : public QDialog {
    Q_OBJECT

public:
    explicit SynthesisResultDialog(const acutesim::compute::orchestration::SynthesisResult& result,
                                   QWidget* parent = nullptr);
};

} // namespace acutesim::gui::orchestration
