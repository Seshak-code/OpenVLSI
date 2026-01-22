#include "monte_carlo_setup_dialog.h"
#include <QLabel>
#include <QGroupBox>
#include <QDoubleSpinBox>

namespace acutesim::gui::analysis {

MonteCarloSetupDialog::MonteCarloSetupDialog(const TensorNetlist& netlist, QWidget* parent)
    : QDialog(parent), netlist_(netlist)
{
    setWindowTitle("Advanced Monte Carlo Setup");
    resize(600, 450);
    setupUI();
    populateTable();
}

void MonteCarloSetupDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // 1. General Settings
    auto* settingsGroup = new QGroupBox("General Settings", this);
    auto* formLayout = new QFormLayout(settingsGroup);
    
    spinRuns_ = new QSpinBox(this);
    spinRuns_->setRange(10, 100000);
    spinRuns_->setValue(100);
    formLayout->addRow("Number of Runs:", spinRuns_);
    
    comboStrategy_ = new QComboBox(this);
    comboStrategy_->addItem("Latin Hypercube (LHS)", static_cast<int>(Statistics::SamplingStrategy::LATIN_HYPERCUBE));
    comboStrategy_->addItem("Random (Naive)", static_cast<int>(Statistics::SamplingStrategy::RANDOM));
    formLayout->addRow("Sampling Strategy:", comboStrategy_);
    
    comboTarget_ = new QComboBox(this);
    // Add Probes
    for (const auto& p : netlist_.globalBlock.probes) {
        comboTarget_->addItem(QString("Probe: %1 (V)").arg(QString::fromStdString(p.name)), p.nodePositive);
    }
    // Add all nodes
    for (int i = 1; i < (int)netlist_.numGlobalNodes; ++i) {
        comboTarget_->addItem(QString("Node %1 Voltage").arg(i), i);
    }
    formLayout->addRow("Measurement Target:", comboTarget_);
    
    layout->addWidget(settingsGroup);
    
    // 2. Component Variations
    layout->addWidget(new QLabel("Variation Definition (Tolerances):"));
    
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Parameter", "Base Value", "Tolerance (%)", "Distribution"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table_);
    
    // 3. Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void MonteCarloSetupDialog::populateTable() {
    int row = 0;
    
    // Add Resistors
    for (const auto& r : netlist_.globalBlock.resistors) {
        table_->insertRow(row);
        
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(r.name) + ".resistance");
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, nameItem);
        
        auto* valItem = new QTableWidgetItem(QString::number(r.resistance_ohms));
        valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 1, valItem);
        
        auto* spinTol = new QDoubleSpinBox();
        spinTol->setRange(0, 100);
        spinTol->setValue(5.0); // Default 5%
        table_->setCellWidget(row, 2, spinTol);
        
        auto* comboDist = new QComboBox();
        comboDist->addItem("Gaussian", static_cast<int>(Statistics::DistributionType::GAUSSIAN));
        comboDist->addItem("Uniform", static_cast<int>(Statistics::DistributionType::UNIFORM));
        table_->setCellWidget(row, 3, comboDist);
        
        row++;
    }
    
    // Add Capacitors
    for (const auto& c : netlist_.globalBlock.capacitors) {
        table_->insertRow(row);
        
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(c.name) + ".capacitance");
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, nameItem);
        
        auto* valItem = new QTableWidgetItem(QString::number(c.capacitance_farads));
        valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 1, valItem);
        
        auto* spinTol = new QDoubleSpinBox();
        spinTol->setRange(0, 100);
        spinTol->setValue(10.0); // Caps often have wider tol
        table_->setCellWidget(row, 2, spinTol);
        
        auto* comboDist = new QComboBox();
        comboDist->addItem("Gaussian", static_cast<int>(Statistics::DistributionType::GAUSSIAN));
        comboDist->addItem("Uniform", static_cast<int>(Statistics::DistributionType::UNIFORM));
        table_->setCellWidget(row, 3, comboDist);
        
        row++;
    }
}

int MonteCarloSetupDialog::numRuns() const {
    return spinRuns_->value();
}

int MonteCarloSetupDialog::targetNode() const {
    return comboTarget_->currentData().toInt();
}

Statistics::SamplingStrategy MonteCarloSetupDialog::strategy() const {
    return static_cast<Statistics::SamplingStrategy>(comboStrategy_->currentData().toInt());
}

std::map<std::string, Statistics::Distribution> MonteCarloSetupDialog::variations() const {
    std::map<std::string, Statistics::Distribution> vars;
    
    for (int i = 0; i < table_->rowCount(); ++i) {
        QString key = table_->item(i, 0)->text();
        double baseVal = table_->item(i, 1)->text().toDouble();
        
        auto* spinTol = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(i, 2));
        double tolPercent = spinTol->value();
        
        auto* comboDist = qobject_cast<QComboBox*>(table_->cellWidget(i, 3));
        auto type = static_cast<Statistics::DistributionType>(comboDist->currentData().toInt());
        
        if (tolPercent <= 0) continue;
        
        if (type == Statistics::DistributionType::GAUSSIAN) {
            // tolPercent is 3-sigma
            double sigma = (baseVal * (tolPercent / 100.0)) / 3.0;
            vars[key.toStdString()] = Statistics::Distribution::Gaussian(baseVal, sigma);
        } else {
            // UNIFORM
            double delta = baseVal * (tolPercent / 100.0);
            vars[key.toStdString()] = Statistics::Distribution::Uniform(baseVal - delta, baseVal + delta);
        }
    }
    
    return vars;
}

} // namespace acutesim::gui::analysis
