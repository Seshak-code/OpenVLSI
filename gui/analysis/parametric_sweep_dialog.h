#pragma once
#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include "compute/orchestration/sweep_parameter.h"

namespace acutesim::gui::analysis {

class ParametricSweepDialog : public QDialog {
    Q_OBJECT
public:
    explicit ParametricSweepDialog(const QStringList& variables, QWidget* parent = nullptr);
    
    acutesim::compute::orchestration::SweepParameter getSweepParameter() const;

private:
    QComboBox* varCombo_;
    QDoubleSpinBox* startSpin_;
    QDoubleSpinBox* stopSpin_;
    QDoubleSpinBox* stepSpin_;
    QCheckBox* logCheck_;
};

} // namespace acutesim::gui::analysis
