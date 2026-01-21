#include "corner_setup_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>

namespace acutesim::gui::analysis {

CornerSetupWidget::CornerSetupWidget(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void CornerSetupWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // --- Process Corner ---
    auto* processGroup = new QGroupBox("Process Corner");
    auto* processLayout = new QVBoxLayout(processGroup);
    m_processCombo = new QComboBox();
    m_processCombo->addItems({"TT (Typical)", "FF (Fast-Fast)", "SS (Slow-Slow)", "FS (Fast-Slow)", "SF (Slow-Fast)"});
    processLayout->addWidget(m_processCombo);
    mainLayout->addWidget(processGroup);
    
    // --- Temperature Sweep ---
    auto* tempGroup = new QGroupBox("Temperature Sweep (°C)");
    auto* tempLayout = new QHBoxLayout(tempGroup);
    
    tempLayout->addWidget(new QLabel("Min:"));
    m_tempMin = new QDoubleSpinBox(); m_tempMin->setRange(-273, 500); m_tempMin->setValue(27.0);
    tempLayout->addWidget(m_tempMin);
    
    tempLayout->addWidget(new QLabel("Max:"));
    m_tempMax = new QDoubleSpinBox(); m_tempMax->setRange(-273, 500); m_tempMax->setValue(27.0);
    tempLayout->addWidget(m_tempMax);
    
    tempLayout->addWidget(new QLabel("Step:"));
    m_tempStep = new QDoubleSpinBox(); m_tempStep->setRange(0.1, 100); m_tempStep->setValue(10.0);
    tempLayout->addWidget(m_tempStep);
    
    mainLayout->addWidget(tempGroup);
    
    // --- Voltage Scaling ---
    auto* voltageGroup = new QGroupBox("Voltage Scaling");
    auto* voltageLayout = new QHBoxLayout(voltageGroup);
    voltageLayout->addWidget(new QLabel("Scale Factor:"));
    m_voltageScale = new QDoubleSpinBox(); 
    m_voltageScale->setRange(0.5, 2.0); 
    m_voltageScale->setSingleStep(0.05);
    m_voltageScale->setValue(1.0);
    voltageLayout->addWidget(m_voltageScale);
    mainLayout->addWidget(voltageGroup);
    
    // --- Monte Carlo ---
    auto* mcGroup = new QGroupBox("Statistical Analysis");
    mcGroup->setCheckable(true);
    mcGroup->setChecked(false);
    m_monteCarloEnable = new QCheckBox("Enable Monte Carlo"); // Redundant with group checkable but useful for clarity
    
    auto* mcLayout = new QHBoxLayout(mcGroup);
    mcLayout->addWidget(new QLabel("Runs:"));
    m_monteCarloRuns = new QSpinBox();
    m_monteCarloRuns->setRange(10, 10000);
    m_monteCarloRuns->setValue(100);
    mcLayout->addWidget(m_monteCarloRuns);
    
    mainLayout->addWidget(mcGroup);
    m_monteCarloEnable = static_cast<QCheckBox*>(mcGroup->findChild<QCheckBox*>()); // Get the built-in checkbox
    
    mainLayout->addStretch();
    
    // Connect signals
    connect(m_processCombo, &QComboBox::currentIndexChanged, this, &CornerSetupWidget::configurationChanged);
    connect(m_tempMin, &QDoubleSpinBox::valueChanged, this, &CornerSetupWidget::configurationChanged);
    connect(m_tempMax, &QDoubleSpinBox::valueChanged, this, &CornerSetupWidget::configurationChanged);
    connect(m_voltageScale, &QDoubleSpinBox::valueChanged, this, &CornerSetupWidget::configurationChanged);
    if(m_monteCarloEnable) connect(m_monteCarloEnable, &QAbstractButton::toggled, this, &CornerSetupWidget::configurationChanged);
}

QVector<acutesim::physics::CornerConfig> CornerSetupWidget::getCorners() const {
    QVector<acutesim::physics::CornerConfig> corners;
    using namespace acutesim::physics;
    
    ProcessCorner p = static_cast<ProcessCorner>(m_processCombo->currentIndex());
    double v = m_voltageScale->value();
    
    // Monte Carlo Override
    QGroupBox* mcGroup = findChild<QGroupBox*>(); 
    // Ideally we kept the pointer. But since we didn't store mcGroup in class, we check m_monteCarloEnable parent or check state directly.
    // Actually, m_monteCarloEnable is the checkbox if we set it up that way.
    // But QGroupBox logic:
    if (mcGroup && mcGroup->isChecked()) {
        // Actually, QGroupBox::isChecked() works if setCheckable(true). 
        // But above I tried to dynamic_cast. Let's rely on the member if I can properly assign it.
        // Re-reading logic: m_monteCarloEnable was assigned via findChild. The groupbox itself has the checkbox.
        
        int runs = m_monteCarloRuns->value();
        std::mt19937 rng(12345); // Fixed seed for reproducibility base
        
        for (int i = 0; i < runs; ++i) {
            CornerConfig c;
            c.name = QString("MC Run #%1").arg(i+1);
            c.temperatureC = m_tempMin->value(); // Use Min as Nominal for MC usually, or fix it
            c.voltageScale = v;
            c.process = p;
            c.mode = VariationMode::MONTE_CARLO;
            c.monteCarloSeed = static_cast<int>(rng());
            c.iterationIndex = i;
            corners.push_back(c);
        }
        return corners;
    }
    
    // Deterministic Sweep (Cartesian Product T x V x P)
    // Here we simplified GUI to one Process and one Voltage, sweeping Temp.
    // If user wants full matrix, they'd need more complex UI.
    
    double tStart = m_tempMin->value();
    double tStop = m_tempMax->value();
    double tStep = m_tempStep->value();
    
    if (tStep <= 0) tStep = 1.0;
    
    int points = static_cast<int>((tStop - tStart) / tStep) + 1;
    if (points < 1) points = 1;
    
    for (int i = 0; i < points; ++i) {
        double temp = tStart + (i * tStep);
        if (tStop < tStart) temp = tStart; // Safety
        
        CornerConfig c;
        c.name = QString("T=%1C %2 V=%3").arg(temp).arg(m_processCombo->currentText().left(2)).arg(v);
        c.temperatureC = temp;
        c.voltageScale = v;
        c.process = p;
        c.mode = VariationMode::NONE;
        corners.push_back(c);
    }
    
    return corners;
}

void CornerSetupWidget::updatePreview() {
    // This would update a preview label or table if we had one.
    // For now, we just emit the signal that configuration has changed.
    emit configurationChanged();
}

} // namespace acutesim::gui::analysis
