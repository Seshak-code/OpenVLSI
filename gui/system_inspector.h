#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QLabel>
#include "../components/circuit.h" // For Enums/Structs

class SystemInspector : public QWidget {
    Q_OBJECT
public:
    explicit SystemInspector(QWidget *parent = nullptr);
    
    // Struct to hold state (synced with global SimulationEnvironment)
    struct State {
        double vdd_voltage = 1.8;
        double ripple_vpp = 0.05;
        double esr = 0.05;
        double decap = 100e-9;
        double temp_C = 25.0;
        bool thermal_noise = true;
        bool high_precision = true; // [NEW] Enable Dual-Scale Accumulation
    };
    
    State getState() const;
    void applyState(const State& s);

signals:
    void stateChanged(const State& newState);

private:
    void setupUi();
    void updateLabels();

private:
    // Power Rail Controls
    QGroupBox* powerGroup;
    QComboBox* railSelector;
    QDoubleSpinBox* vddSpin;
    QDoubleSpinBox* rippleSpin;
    QDoubleSpinBox* esrSpin;
    QDoubleSpinBox* decapSpin;
    
    // Environment Controls
    QGroupBox* envGroup;
    QSlider* tempSlider;
    QLabel* tempLabel;
    QComboBox* processCornerCombo;
    QCheckBox* noiseCheck;
    QCheckBox* precisionCheck; // [NEW]
};
