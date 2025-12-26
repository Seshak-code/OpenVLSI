#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QString>
#include <QTimer>
#include "circuit.h"
#include "circuitsim.h"

class SimulatorWindow : public QWidget {
    Q_OBJECT // Macro required for signals & slots

public:
    SimulatorWindow(QWidget *parent = nullptr);
    ~SimulatorWindow() = default;

private slots:
    void startSimulation();
    void stepSimulation();

private:
    QPlainTextEdit* logArea;
    QTimer* simTimer;
    
    // Simulation State
    TensorNetlist netlist;
    CircuitSim sim;
    SimulationTrace trace;
    double currentTime = 0.0;
    const double timeStep = 0.00001; // 10us
    const double totalTime = 0.002;  // 2ms
};
