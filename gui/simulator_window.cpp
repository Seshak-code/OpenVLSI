#include "simulator_window.h"

SimulatorWindow::SimulatorWindow(QWidget *parent) : QWidget(parent) {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Title
  auto *title = new QLabel("⚡ Simulation Results", this);
  title->setStyleSheet("font-size: 20px; font-weight: bold; color: #4CAF50;");
  mainLayout->addWidget(title);

  // Status Label
  statusLabel = new QLabel("Ready. Click 'Run Simulation' to start.", this);
  statusLabel->setStyleSheet("font-size: 14px; color: #888;");
  mainLayout->addWidget(statusLabel);

  // Log Area
  logArea = new QPlainTextEdit(this);
  logArea->setReadOnly(true);
  logArea->setPlaceholderText("Simulation output will appear here...");
  logArea->setStyleSheet(
      "QPlainTextEdit { background: #1e1e1e; color: #00ff00; font-family: monospace; font-size: 12px; border: 1px solid #444; }");
  mainLayout->addWidget(logArea, 1); // Stretch

  // Run Button
  btnRun = new QPushButton("▶ Run Simulation", this);
  btnRun->setStyleSheet(
      "QPushButton { background-color: #4CAF50; color: white; padding: 12px; font-size: 14px; border-radius: 5px; }"
      "QPushButton:hover { background-color: #45a049; }");
  mainLayout->addWidget(btnRun);

  connect(btnRun, &QPushButton::clicked, this, &SimulatorWindow::startSimulation);

  simTimer = new QTimer(this);
  connect(simTimer, &QTimer::timeout, this, &SimulatorWindow::stepSimulation);
}

void SimulatorWindow::startSimulation() {
  logArea->clear();
  
  if (netlist.numGlobalNodes == 0) {
    logArea->appendPlainText("ERROR: No netlist provided!");
    logArea->appendPlainText("Please draw a circuit in the Schematic tab and click 'Run Simulation' from the menu.");
    statusLabel->setText("❌ No circuit to simulate.");
    return;
  }

  statusLabel->setText("🔄 Simulating...");
  btnRun->setEnabled(false);

  logArea->appendPlainText("=== Netlist Summary ===");
  logArea->appendPlainText(QString("  Nodes: %1").arg(netlist.numGlobalNodes));
  logArea->appendPlainText(QString("  Resistors: %1").arg(netlist.globalBlock.resistors.size()));
  logArea->appendPlainText(QString("  Capacitors: %1").arg(netlist.globalBlock.capacitors.size()));
  logArea->appendPlainText(QString("  Voltage Sources: %1").arg(netlist.globalBlock.voltageSources.size()));
  logArea->appendPlainText(QString("  Diodes: %1").arg(netlist.globalBlock.diodes.size()));
  logArea->appendPlainText("");

  // Reset Sim State
  sim = CircuitSim();
  sim.resetArbiter();
  currentTime = 0.0;
  trace.clear();
  trace.netlistName = "Schematic Circuit";

  logArea->appendPlainText("=== Transient Analysis ===");
  logArea->appendPlainText("Time(ms)      V(Node 1)");
  logArea->appendPlainText("-------------------------");

  // Start the non-blocking loop
  simTimer->start(0);
}

void SimulatorWindow::stepSimulation() {
  // Run a batch of steps per frame to keep UI responsive
  for (int i = 0; i < 100; ++i) {
    if (currentTime >= totalTime) {
      simTimer->stop();
      logArea->appendPlainText("");
      logArea->appendPlainText("=== Simulation Complete ===");
      statusLabel->setText("✅ Simulation finished.");
      btnRun->setEnabled(true);
      return;
    }

    // Perform one physics step
    SimulationStep step = sim.stepTransient(netlist, timeStep, currentTime);
    
    // Update state based on actual step taken
    double actualDt = step.time - currentTime;
    currentTime = step.time;

    trace.addStep(currentTime, step.nodeVoltages, step.stats);

    // Log output (decimate to avoid spamming the logs)
    if (std::abs(std::fmod(currentTime, 0.0001)) < timeStep / 2.0) {
      double v_out = 0.0;
      if (step.nodeVoltages.size() >= 1)
        v_out = step.nodeVoltages[0];

      QString logLine = QString("%1         %2 V")
                            .arg(currentTime * 1000, 6, 'f', 3)
                            .arg(v_out, 8, 'f', 4);
      logArea->appendPlainText(logLine);
    }
  }
}
