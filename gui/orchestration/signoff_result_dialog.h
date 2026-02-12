#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "compute/orchestration/drivers/klayout_report_parser.h"

namespace acutesim::gui::orchestration {

/**
 * @brief Modeless dialog showing KLayout DRC or LVS sign-off results.
 *
 * DRC view: Summary traffic light + violations table (rule, layer, count, severity).
 * LVS view: Match/mismatch indicator + device/net counts + mismatch detail list.
 */
class SignOffResultDialog : public QDialog {
    Q_OBJECT

public:
    explicit SignOffResultDialog(const acutesim::compute::orchestration::DRCResult& result,
                                QWidget* parent = nullptr);

    explicit SignOffResultDialog(const acutesim::compute::orchestration::LVSResult& result,
                                QWidget* parent = nullptr);

private:
    void buildDRCView(const acutesim::compute::orchestration::DRCResult& result);
    void buildLVSView(const acutesim::compute::orchestration::LVSResult& result);
    void setupCommonLayout();

    QTabWidget* m_tabs = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;
};

} // namespace acutesim::gui::orchestration
