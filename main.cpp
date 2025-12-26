#include <QApplication>
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
public:
    SimulatorWindow() {
        setWindowTitle("CuteApp - Circuit Simulator (Wasm Ready)");
        resize(600, 400);

        auto* layout = new QVBoxLayout(this);

        auto* title = new QLabel("RC Circuit: Step Response (5V Input, 1kΩ, 1µF)", this);
        title->setStyleSheet("font-size: 16px; font-weight: bold; margin-bottom: 10px;");
        layout->addWidget(title);

        logArea = new QPlainTextEdit(this);
        logArea->setReadOnly(true);
        logArea->setPlaceholderText("Simulation results will appear here...");
        layout->addWidget(logArea);

        auto* btnRun = new QPushButton("Run Simulation", this);
        btnRun->setStyleSheet("background-color: #4CAF50; color: white; padding: 10px; font-size: 14px;");
        layout->addWidget(btnRun);

        connect(btnRun, &QPushButton::clicked, this, &SimulatorWindow::startSimulation);

        simTimer = new QTimer(this);
        connect(simTimer, &QTimer::timeout, this, &SimulatorWindow::stepSimulation);
    }

private:
    QPlainTextEdit* logArea;
    QTimer* simTimer;
    
    // Simulation State
    TensorNetlist netlist;
    CircuitSim sim;
    double currentTime = 0.0;
    const double timeStep = 0.00001; // 10us
    const double totalTime = 0.002;  // 2ms

    void startSimulation() {
        logArea->clear();
        logArea->appendPlainText("Initializing Hierarchical Netlist...");

        // --- Build or Reset the Hierarchical Netlist ---
        netlist = TensorNetlist(); // Clear previous state
        
        // Add components to the global block for now (flattened test)
        netlist.addVoltageSource(1, 0, 5.0);
        logArea->appendPlainText("Added Voltage Source (Global): 5V");

        netlist.addResistor(1, 2, 1000.0);
        logArea->appendPlainText("Added Resistor (Global): 1kΩ");

        netlist.addCapacitor(2, 0, 1e-6);
        logArea->appendPlainText("Added Capacitor (Global): 1µF");
        
        // Reset Sim State
        sim = CircuitSim();
        currentTime = 0.0;
        
        logArea->appendPlainText("\nStarting Transient Analysis...");
        logArea->appendPlainText("Time(ms) \t V_out (Node 2)");
        logArea->appendPlainText("--------------------------------");
        logArea->appendPlainText(QString("0.000 \t 0.0000 V"));

        // Start the non-blocking loop
        simTimer->start(0); 
    }

    void stepSimulation() {
        // Run a batch of steps (e.g., 100) per frame to keep UI responsive but simulation fast
        for (int i = 0; i < 100; ++i) {
            if (currentTime >= totalTime) {
                simTimer->stop();
                logArea->appendPlainText("\nSimulation Complete.");
                return;
            }

            // Perform one physics step
            SimulationResult result = sim.stepTransient(netlist, timeStep);
            currentTime += timeStep;

            // Log output (decimate to avoid spamming the logs)
             // Print only every 10th step (every 0.1ms)
            if (std::abs(std::fmod(currentTime, 0.0001)) < timeStep / 2.0) {
                 double v_out = 0.0;
                 if (result.nodeVoltages.size() >= 2) v_out = result.nodeVoltages[1];

                 QString logLine = QString("%1 \t %2 V")
                    .arg(currentTime * 1000, 0, 'f', 3)
                    .arg(v_out, 0, 'f', 4);
                logArea->appendPlainText(logLine);
            }
        }
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    SimulatorWindow window;
    window.show();
    
    return app.exec();
}
