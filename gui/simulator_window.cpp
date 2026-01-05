#include "simulator_window.h"
#include <QSplitter>

SimulatorWindow::SimulatorWindow(QWidget *parent) : QWidget(parent) {
  setWindowTitle("CuteApp - Circuit Simulator (Wasm Ready)");
  resize(1000, 600);

  auto *mainLayout = new QHBoxLayout(this);

  // Splitter for Schematic and Log
  auto *splitter = new QSplitter(Qt::Horizontal, this);

  // Left: Schematic Editor
  schematic = new SchematicEditor(this);
  splitter->addWidget(schematic);

  // Right: Log and Controls
  auto *rightPanel = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(5, 5, 5, 5);

  auto *title = new QLabel("Simulation Output", rightPanel);
  title->setStyleSheet(
      "font-size: 16px; font-weight: bold; margin-bottom: 10px;");
  rightLayout->addWidget(title);

  logArea = new QPlainTextEdit(rightPanel);
  logArea->setReadOnly(true);
  logArea->setPlaceholderText("Simulation results will appear here...");
  rightLayout->addWidget(logArea);

  auto *btnRun = new QPushButton("Run Simulation", rightPanel);
  btnRun->setStyleSheet("background-color: #4CAF50; color: white; padding: "
                        "10px; font-size: 14px;");
  rightLayout->addWidget(btnRun);

  splitter->addWidget(rightPanel);
  splitter->setSizes({600, 400}); // Default sizes

  mainLayout->addWidget(splitter);

  connect(btnRun, &QPushButton::clicked, this,
          &SimulatorWindow::startSimulation);

  simTimer = new QTimer(this);
  connect(simTimer, &QTimer::timeout, this, &SimulatorWindow::stepSimulation);
}

void SimulatorWindow::startSimulation() {
  logArea->clear();
  logArea->appendPlainText("Building Netlist from Schematic...");

  // Convert Schematic to Netlist
  netlist = schematic->toNetlist();

  if (netlist.numGlobalNodes == 0) {
    logArea->appendPlainText("ERROR: No components placed on schematic!");
    return;
  }

  logArea->appendPlainText(QString("  Nodes: %1").arg(netlist.numGlobalNodes));
  logArea->appendPlainText(
      QString("  Resistors: %1").arg(netlist.globalBlock.resistors.size()));
  logArea->appendPlainText(
      QString("  Capacitors: %1").arg(netlist.globalBlock.capacitors.size()));
  logArea->appendPlainText(
      QString("  Diodes: %1").arg(netlist.globalBlock.diodes.size()));
  logArea->appendPlainText(QString("  Voltage Sources: %1")
                               .arg(netlist.globalBlock.voltageSources.size()));

  // Reset Sim State
  sim = CircuitSim();
  sim.resetArbiter();
  currentTime = 0.0;
  trace.clear();
  trace.netlistName = "Schematic Circuit";

  logArea->appendPlainText("\nStarting Transient Analysis...");
  logArea->appendPlainText("Time(ms) \t V(Node 1)");
  logArea->appendPlainText("--------------------------------");

  // Start the non-blocking loop
  simTimer->start(0);
}

void SimulatorWindow::stepSimulation() {
  // Run a batch of steps (e.g., 100) per frame to keep UI responsive
  for (int i = 0; i < 100; ++i) {
    if (currentTime >= totalTime) {
      simTimer->stop();
      logArea->appendPlainText("\nSimulation Complete.");
      return;
    }

    // Perform one physics step
    SimulationStep step = sim.stepTransient(netlist, timeStep, currentTime);
    trace.addStep(currentTime, step.nodeVoltages, step.stats);

    currentTime += timeStep;

    // Log output (decimate to avoid spamming the logs)
    if (std::abs(std::fmod(currentTime, 0.0001)) < timeStep / 2.0) {
      double v_out = 0.0;
      if (step.nodeVoltages.size() >= 1)
        v_out = step.nodeVoltages[0];

      QString logLine = QString("%1 \t %2 V")
                            .arg(currentTime * 1000, 0, 'f', 3)
                            .arg(v_out, 0, 'f', 4);
      logArea->appendPlainText(logLine);
    }
  }
}
