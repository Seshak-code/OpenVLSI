#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QString>
#include "circuitsim.h"

class SimulatorWindow : public QWidget {
public:
    SimulatorWindow() {
        setWindowTitle("CuteApp - Circuit Simulator");
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

        connect(btnRun, &QPushButton::clicked, this, &SimulatorWindow::runSimulation);
    }

private:
    QPlainTextEdit* logArea;

    void runSimulation() {
        logArea->clear();
        logArea->appendPlainText("Initializing Circuit...");

        CircuitSim sim;
        
        // Build RC Low-Pass Filter
        // Node 0: Ground
        // Node 1: Input Source (5V Step)
        // Node 2: Output (across Capacitor)
        
        // 1. Voltage Source: 5V DC step at Node 1
        sim.addVoltageSource(1, 0, 5.0);
        logArea->appendPlainText("Added Voltage Source: 5V (Node 1 -> GND)");

        // 2. Resistor: 1kΩ between Node 1 and Node 2
        sim.addResistor(1, 2, 1000.0);
        logArea->appendPlainText("Added Resistor: 1kΩ (Node 1 -> Node 2)");

        // 3. Capacitor: 1µF between Node 2 and Ground
        sim.addCapacitor(2, 0, 1e-6); // 1uF
        logArea->appendPlainText("Added Capacitor: 1µF (Node 2 -> GND)");
        
        double timeStep = 0.00001; // 0.01ms (10us) - Higher resolution
        double totalTime = 0.002; // 2ms
        
        logArea->appendPlainText("\nStarting Transient Analysis (DC Step Response)...");
        logArea->appendPlainText("Time(ms) \t V_out (Node 2)");
        logArea->appendPlainText("--------------------------------");
        
        // Print Initial Condition (t=0)
        logArea->appendPlainText(QString("0.000 \t 0.0000 V"));

        double currentTime = 0.0;
        
        // Simulation Loop
        while (currentTime < totalTime) {
            // Step the simulation
            std::vector<double> voltages = sim.stepTransient(timeStep);
            
            currentTime += timeStep; // Increment time AFTER step

            // Extract Node 2 output
            if (voltages.size() >= 2) {
                double v_out = voltages[1]; // V_node2
                
                // Print only every 10th step to keep log readable (every 0.1ms)
                // Or just print all if short. Let's print every 0.1ms
                if (std::abs(std::fmod(currentTime, 0.1)) < timeStep / 2.0 || currentTime < 0.05) {
                     QString logLine = QString("%1 \t %2 V")
                        .arg(currentTime * 1000, 0, 'f', 3)
                        .arg(v_out, 0, 'f', 4);
                    logArea->appendPlainText(logLine);
                }
            }
        }
        
        logArea->appendPlainText("\nSimulation Complete.");
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    SimulatorWindow window;
    window.show();
    
    return app.exec();
}
