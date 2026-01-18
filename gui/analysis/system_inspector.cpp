#include "system_inspector.h"
#include <QFormLayout>

SystemInspector::SystemInspector(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void SystemInspector::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(15);
    
    // --- Power Rail Group ---
    powerGroup = new QGroupBox("Power Rail Config", this);
    QVBoxLayout* powerLayout = new QVBoxLayout(powerGroup);
    
    railSelector = new QComboBox(powerGroup);
    railSelector->addItems({"VDD_CORE (1.8V)", "VDD_IO (3.3V)", "VSS (GND)"});
    
    QFormLayout* powerForm = new QFormLayout();
    
    vddSpin = new QDoubleSpinBox();
    vddSpin->setRange(0.0, 24.0);
    vddSpin->setSuffix(" V");
    vddSpin->setValue(1.8);
    vddSpin->setSingleStep(0.1);
    
    rippleSpin = new QDoubleSpinBox();
    rippleSpin->setRange(0.0, 1000.0);
    rippleSpin->setSuffix(" mV");
    rippleSpin->setValue(50.0);
    
    esrSpin = new QDoubleSpinBox();
    esrSpin->setRange(0.001, 10.0);
    esrSpin->setSuffix(" Ohm");
    esrSpin->setDecimals(3);
    esrSpin->setValue(0.05);
    
    decapSpin = new QDoubleSpinBox();
    decapSpin->setRange(0.0, 1000.0);
    decapSpin->setSuffix(" nF");
    decapSpin->setValue(100.0);
    
    powerForm->addRow("Rail:", railSelector);
    powerForm->addRow("Nominal Voltage:", vddSpin);
    powerForm->addRow("Ripple (Vpp):", rippleSpin);
    powerForm->addRow("ESR:", esrSpin);
    powerForm->addRow("Decap:", decapSpin);
    
    powerLayout->addLayout(powerForm);
    mainLayout->addWidget(powerGroup);
    
    // --- Environment Group ---
    envGroup = new QGroupBox("Environment", this);
    QVBoxLayout* envLayout = new QVBoxLayout(envGroup);
    QFormLayout* envForm = new QFormLayout();
    
    tempSlider = new QSlider(Qt::Horizontal);
    tempSlider->setRange(-40, 125);
    tempSlider->setValue(25);
    
    tempLabel = new QLabel("25 C");
    
    processCornerCombo = new QComboBox();
    processCornerCombo->addItems({"TT (Typical)", "FF (Fast-Fast)", "SS (Slow-Slow)", "SF (Slow-Fast)", "FS (Fast-Slow)"});
    
    noiseCheck = new QCheckBox("Thermal Noise Enabled");
    noiseCheck->setChecked(true);
    
    precisionCheck = new QCheckBox("High Precision (Dual-Scale)");
    precisionCheck->setToolTip("Required for System Integration (1nm leakage + 12V rails)");
    precisionCheck->setChecked(true);
    
    // Custom Layout for Slider + Label
    QHBoxLayout* tempLayout = new QHBoxLayout();
    tempLayout->addWidget(tempSlider);
    tempLayout->addWidget(tempLabel);
    
    envForm->addRow("Temperature:", tempLayout);
    envForm->addRow("Process:", processCornerCombo);
    envForm->addRow("Noise:", noiseCheck);
    envForm->addRow("Solver:", precisionCheck);
    
    envLayout->addLayout(envForm);
    mainLayout->addWidget(envGroup);
    
    mainLayout->addStretch();
    
    // --- Connections ---
    auto emitChange = [this]() { emit stateChanged(getState()); };
    
    connect(vddSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), emitChange);
    connect(rippleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), emitChange);
    connect(esrSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), emitChange);
    connect(decapSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), emitChange);
    
    connect(tempSlider, &QSlider::valueChanged, this, [this, emitChange](int val){
        updateLabels();
        emitChange();
    });
    
    connect(processCornerCombo, &QComboBox::currentIndexChanged, emitChange);
    connect(noiseCheck, &QCheckBox::toggled, emitChange);
    connect(precisionCheck, &QCheckBox::toggled, emitChange);
}

void SystemInspector::updateLabels() {
    tempLabel->setText(QString("%1 C").arg(tempSlider->value()));
}

SystemInspector::State SystemInspector::getState() const {
    State s;
    s.vdd_voltage = vddSpin->value();
    s.ripple_vpp = rippleSpin->value() / 1000.0; // Convert mV to V
    s.esr = esrSpin->value();
    s.decap = decapSpin->value() * 1e-9; // Convert nF to F
    s.temp_C = (double)tempSlider->value();
    s.thermal_noise = noiseCheck->isChecked();
    s.high_precision = precisionCheck->isChecked();
    return s;
}

void SystemInspector::applyState(const State& s) {
    vddSpin->setValue(s.vdd_voltage);
    rippleSpin->setValue(s.ripple_vpp * 1000.0);
    esrSpin->setValue(s.esr);
    decapSpin->setValue(s.decap * 1e9);
    tempSlider->setValue((int)s.temp_C);
    noiseCheck->setChecked(s.thermal_noise);
    precisionCheck->setChecked(s.high_precision);
    updateLabels();
}
