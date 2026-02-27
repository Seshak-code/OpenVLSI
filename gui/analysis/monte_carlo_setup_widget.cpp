#include "monte_carlo_setup_widget.h"
#include <QLabel>
#include <QGroupBox>
#include <QDoubleSpinBox>

namespace acutesim::gui::analysis {

MonteCarloSetupWidget::MonteCarloSetupWidget(const acutesim::compute::orchestration::HierarchicalSchematicDTO& hierDto, QWidget* parent)
    : QWidget(parent), hierDto_(hierDto)
{
    setupUI();
    populateTable();
}

void MonteCarloSetupWidget::setupUI() {
    auto* layout = new QVBoxLayout(this);

    // 1. General Settings
    auto* settingsGroup = new QGroupBox("General Settings", this);
    auto* formLayout = new QFormLayout(settingsGroup);

    spinRuns_ = new QSpinBox(this);
    spinRuns_->setRange(10, 100000);
    spinRuns_->setValue(100);
    formLayout->addRow("Number of Runs:", spinRuns_);

    comboStrategy_ = new QComboBox(this);
    comboStrategy_->addItem("Latin Hypercube (LHS)", 0);
    comboStrategy_->addItem("Random (Naive)", 1);
    formLayout->addRow("Sampling Strategy:", comboStrategy_);

    comboTarget_ = new QComboBox(this);
    // Add nets as measurement targets
    for (const auto& net : hierDto_.topGraph.nets) {
        comboTarget_->addItem(QString("Net: %1 (V)").arg(QString::fromStdString(net.name)), QString::fromStdString(net.name));
    }
    
    // Add explicit probes found in instances (if any)
    for (const auto& inst : hierDto_.topGraph.instances) {
        if (inst.type == "Probe") {
            comboTarget_->addItem(QString("Probe: %1").arg(QString::fromStdString(inst.instanceName)), QString::fromStdString(inst.instanceName));
        }
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
}

void MonteCarloSetupWidget::populateTable() {
    int row = 0;

    for (const auto& inst : hierDto_.topGraph.instances) {
        // Find parameters that make sense to vary
        for (const auto& [paramName, value] : inst.parameters) {
            bool shouldVary = false;
            double defaultTol = 5.0;

            if (paramName == "R" || paramName == "val" && inst.type == "Resistor") shouldVary = true;
            else if (paramName == "C" || paramName == "val" && inst.type == "Capacitor") { shouldVary = true; defaultTol = 10.0; }
            else if (paramName == "L" || paramName == "val" && inst.type == "Inductor") { shouldVary = true; defaultTol = 10.0; }
            else if (paramName == "W" || paramName == "L") shouldVary = true; // MOSFET dims

            if (!shouldVary) continue;

            table_->insertRow(row);

            auto* nameItem = new QTableWidgetItem(QString::fromStdString(inst.instanceName) + "." + QString::fromStdString(paramName));
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, 0, nameItem);

            auto* valItem = new QTableWidgetItem(QString::number(value));
            valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, 1, valItem);

            auto* spinTol = new QDoubleSpinBox();
            spinTol->setRange(0, 100);
            spinTol->setValue(defaultTol);
            table_->setCellWidget(row, 2, spinTol);

            auto* comboDist = new QComboBox();
            comboDist->addItem("Gaussian", 0);
            comboDist->addItem("Uniform", 1);
            table_->setCellWidget(row, 3, comboDist);

            row++;
        }
    }
}

int MonteCarloSetupWidget::numRuns() const {
    return spinRuns_->value();
}

int MonteCarloSetupWidget::targetNode() const {
    // Note: In thin-client DTO paradigm, indices are determined by the orchestrator.
    // We return -1 if using net names, or 0 if user explicitly chose GND.
    QString target = comboTarget_->currentData().toString();
    if (target == "0" || target == "GND") return 0;
    return -1; // Orchestrator must resolve by name
}

std::vector<acutesim::compute::orchestration::MonteCarloVariationDTO> MonteCarloSetupWidget::variations() const {
    std::vector<acutesim::compute::orchestration::MonteCarloVariationDTO> vars;

    for (int i = 0; i < table_->rowCount(); ++i) {
        QString key = table_->item(i, 0)->text();
        double baseVal = table_->item(i, 1)->text().toDouble();

        auto* spinTol = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(i, 2));
        double tolPercent = spinTol->value();

        auto* comboDist = qobject_cast<QComboBox*>(table_->cellWidget(i, 3));
        bool isGaussian = (comboDist->currentData().toInt() == 0);

        if (tolPercent <= 0) continue;
        
        acutesim::compute::orchestration::MonteCarloVariationDTO var;
        
        QStringList parts = key.split(".");
        if (parts.size() >= 2) {
            var.componentName = parts[0].toStdString();
            var.propertyName = parts[1].toStdString();
        } else {
            var.componentName = key.toStdString();
            var.propertyName = "value";
        }
        
        var.distributionType = isGaussian ? "Gaussian" : "Uniform";
        var.nominalValue = baseVal;
        var.tolerance = tolPercent / 100.0;
        
        vars.push_back(var);
    }

    return vars;
}

} // namespace acutesim::gui::analysis
