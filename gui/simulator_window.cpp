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
  logArea->appendPlainText(QString("  Current Sources: %1").arg(netlist.globalBlock.currentSources.size()));
  logArea->appendPlainText(QString("  Diodes: %1").arg(netlist.globalBlock.diodes.size()));
  logArea->appendPlainText(QString("  BJTs: %1").arg(netlist.globalBlock.bjts.size()));
  logArea->appendPlainText(QString("  MOSFETs: %1").arg(netlist.globalBlock.mosfets.size()));
  logArea->appendPlainText("");

  // Reset Sim State
  sim = CircuitSim();
  sim.resetArbiter();
  currentTime = 0.0;
  trace.clear();
  trace.netlistName = "Schematic Circuit";

  logArea->appendPlainText("=== Transient Analysis ===");
  logArea->appendPlainText("Time          Probe Readings");
  logArea->appendPlainText("-----------------------------");

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
    SolverStep step = sim.stepTransient(netlist, timeStep, currentTime);
    
    // Defensive guard: Check for convergence failure
    if (!step.stats.converged) {
      simTimer->stop();
      logArea->appendPlainText("");
      logArea->appendPlainText("=== SIMULATION FAILED ===");
      logArea->appendPlainText(QString("Non-convergence at t=%1 ms").arg(currentTime * 1000, 0, 'f', 4));
      logArea->appendPlainText("Try simplifying the circuit or checking for floating nodes.");
      statusLabel->setText("❌ Simulation failed to converge.");
      btnRun->setEnabled(true);
      return;
    }
    
    // Update state based on actual step taken
    double actualDt = step.time - currentTime;
    currentTime = step.time;

    trace.addStep(currentTime, step.nodeVoltages, step.stats);

    // Log output (decimate to avoid spamming the logs)
    if (std::abs(std::fmod(currentTime, 0.0001)) < timeStep / 2.0) {
      QString logLine = QString("%1 ms | ").arg(currentTime * 1000, 6, 'f', 3);
      
      if (!netlist.globalBlock.probes.empty()) {
        // Show all probes
        for (const auto& prb : netlist.globalBlock.probes) {
          // Node 0 = ground (always 0V), other nodes are 1-indexed in solver
          double v_pos = 0.0;
          double v_neg = 0.0;
          
          // Handle ground node (0) vs circuit nodes (1+)
          if (prb.nodePositive > 0 && prb.nodePositive <= (int)step.nodeVoltages.size()) {
            v_pos = step.nodeVoltages[prb.nodePositive - 1];
          }
          if (prb.nodeNegative > 0 && prb.nodeNegative <= (int)step.nodeVoltages.size()) {
            v_neg = step.nodeVoltages[prb.nodeNegative - 1];
          }
          
          double v_diff = v_pos - v_neg;
          logLine += QString("%1[n%2-n%3]: %4 V  ")
                         .arg(QString::fromStdString(prb.name))
                         .arg(prb.nodePositive)
                         .arg(prb.nodeNegative)
                         .arg(v_diff, 8, 'f', 4);
        }
      } else {
        // Fallback: Show Node 1 if it exists
        double v_out = 0.0;
        if (step.nodeVoltages.size() >= 1)
          v_out = step.nodeVoltages[0];
        logLine += QString("V(1): %1 V").arg(v_out, 8, 'f', 4);
      }

      logArea->appendPlainText(logLine);
    }
  }
}
