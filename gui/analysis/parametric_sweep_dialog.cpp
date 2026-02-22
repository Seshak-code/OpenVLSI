#include "parametric_sweep_dialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>

namespace acutesim::gui::analysis {

ParametricSweepDialog::ParametricSweepDialog(const QStringList& variables, QWidget* parent) 
    : QDialog(parent) 
{
    setWindowTitle("Parametric Sweep");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout();
    
    varCombo_ = new QComboBox(this);
    varCombo_->addItems(variables);
    form->addRow("Variable:", varCombo_);
    
    startSpin_ = new QDoubleSpinBox(this);
    startSpin_->setRange(-1e9, 1e9);
    startSpin_->setValue(0.0);
    form->addRow("Start:", startSpin_);
    
    stopSpin_ = new QDoubleSpinBox(this);
    stopSpin_->setRange(-1e9, 1e9);
    stopSpin_->setValue(10.0);
    form->addRow("Stop:", stopSpin_);
    
    stepSpin_ = new QDoubleSpinBox(this);
    stepSpin_->setRange(0, 1e9);
    stepSpin_->setValue(1.0);
    form->addRow("Step / Points:", stepSpin_);
    
    logCheck_ = new QCheckBox("Logarithmic Sweep", this);
    form->addRow("", logCheck_);
    
    mainLayout->addLayout(form);
    
    QDialogButtonBox* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(bbox);
}

acutesim::compute::orchestration::SweepParameter ParametricSweepDialog::getSweepParameter() const {
    acutesim::compute::orchestration::SweepParameter p;
    p.variableName = varCombo_->currentText();
    p.start = startSpin_->value();
    p.stop = stopSpin_->value();
    p.step = stepSpin_->value();
    p.isLog = logCheck_->isChecked();
    return p;
}

} // namespace acutesim::gui::analysis
