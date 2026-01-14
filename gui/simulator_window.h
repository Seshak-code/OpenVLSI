#pragma once
#include "circuit.h"
#include "circuitsim.h"
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class SimulatorWindow : public QWidget {
Q_OBJECT

public:
  SimulatorWindow(QWidget *parent = nullptr);
  ~SimulatorWindow() = default;

  void setNetlist(const TensorNetlist& n) { netlist = n; }

private slots:
  void startSimulation();
  void stepSimulation();

private:
  QPlainTextEdit *logArea;
  QLabel *statusLabel;
  QPushButton *btnRun;
  QTimer *simTimer;

  // Simulation State
  TensorNetlist netlist;
  CircuitSim sim;
  SolverTrace trace;
  double currentTime = 0.0;
  double timeStep = 0.00001; // 10us
  const double totalTime = 0.002;  // 2ms
};
