#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <atomic>
#include <QWaitCondition>
#include "../../physics/circuitsim.h"
#include "../../physics/simulation_profile.h"
#include "../analysis/erc_engine.h"

class SimulationWorker : public QObject {
    Q_OBJECT
public:
    explicit SimulationWorker(QObject* parent = nullptr);
    ~SimulationWorker();
    
    // Explicit shutdown method to be called before thread destruction
    // Sets stop flag and waits for current operation to yield
    void stopAndJoin();

public slots:
    // Start methods - objects passed by value for thread safety
    void startDC(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode);
    void startTransient(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode);
    
    // Request stop (Thread-safe)
    void stop();

signals:
    // Results
    void resultsReady(const QJsonObject& results);
    void solverViolation(const ERC::Violation& violation);
    void simulationFinished();
    
    // Logging
    void logMessage(const QString& msg);
    void streamMessage(const QString& msg);
    
private:
    CircuitSim sim_;
    
    // Thread synchronization
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    
    // Helper to check safety
    bool isStopRequested() const;
};
