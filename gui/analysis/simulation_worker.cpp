#include "simulation_worker.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>

SimulationWorker::SimulationWorker(QObject* parent) : QObject(parent) {
    // Constructor runs in the creating thread (Main)
}

SimulationWorker::~SimulationWorker() {
    stopAndJoin();
}

void SimulationWorker::stopAndJoin() {
    stop();
    // In a real thread join scenario, QThread::wait() handled by owner
}

void SimulationWorker::stop() {
    stopRequested_.store(true, std::memory_order_relaxed);
}

bool SimulationWorker::isStopRequested() const {
    return stopRequested_.load(std::memory_order_relaxed);
}

void SimulationWorker::startDC(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode) {
    if (running_.exchange(true)) {
        emit logMessage("[Worker] Simulation already in progress.");
        return;
    }
    stopRequested_.store(false);
    
    // Set Execution Mode
    // sim_.execMode is part of CircuitSim. We map RunMode/Profile to CircuitSim internals if needed.
    // For now, assume LEGACY_AOS or GPU based on external config? 
    // Wait, SimulationWorkspace set execMode based on ComboBox.
    // We didn't pass execMode in the signature!
    // Added acutesim::physics::RunMode, but that is FULL/QUICK.
    // We need ComputeEngine enum too? Or stick to default for now?
    // Implementation plan didn't specify ComputeEngine propagation.
    // Let's assume LEGACY for now or deduce from profile.
    
    QList<acutesim::physics::CornerConfig> runCorners = profile.corners;
    if (runCorners.isEmpty()) {
        runCorners.append(acutesim::physics::CornerConfig::Nominal());
    }
    
    emit logMessage(QString("[Worker] Starting DC Analysis (%1 corners)...").arg(runCorners.size()));
    
    bool allSuccess = true;
    
    for (const auto& corner : runCorners) {
        if (isStopRequested()) break;
        
        sim_.applyCorner(netlist, corner);
        emit logMessage(QString("[Worker] Corner: %1").arg(QString::fromStdString(corner.name)));
        
        SolverStep result = sim_.solveDC(netlist);
        
        if (result.stats.converged) {
            // Build Result
            QJsonObject jsonRes;
            jsonRes["type"] = "DC";
            jsonRes["corner"] = QString::fromStdString(corner.name);
            
            QJsonArray voltages;
            for(double v : result.nodeVoltages) voltages.append(v);
            jsonRes["voltages"] = voltages;
            
            // Probes
            QJsonObject probeVals;
            for (const auto& probe : netlist.globalBlock.probes) {
                double v_pos = (probe.nodePositive > 0 && probe.nodePositive <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodePositive-1] : 0.0;
                double v_neg = (probe.nodeNegative > 0 && probe.nodeNegative <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodeNegative-1] : 0.0;
                probeVals[QString::fromStdString(probe.name)] = v_pos - v_neg;
            }
            jsonRes["probes"] = probeVals;
            
            emit resultsReady(jsonRes);
        } else {
            allSuccess = false;
            // Emit Violation
            ERC::ViolationType vType = ERC::ViolationType::SOLVER_DIVERGENCE;
            QString detail = QString::fromStdString(result.stats.error_detail);
            if (detail.contains("Singular Matrix", Qt::CaseInsensitive)) {
                vType = ERC::ViolationType::SOLVER_SINGULAR_MATRIX;
            }
            
            ERC::Violation v(vType, 
                QString("Solver Failed: %1 (Corner %2)").arg(detail).arg(QString::fromStdString(corner.name)), 
                QPoint(-9999, -9999));
            emit solverViolation(v);
        }
    }
    
    running_ = false;
    emit simulationFinished();
}

void SimulationWorker::startTransient(TensorNetlist netlist, Simulation::SimulationProfile profile, acutesim::physics::RunMode mode) {
    if (running_.exchange(true)) {
        emit logMessage("[Worker] Simulation already in progress.");
        return;
    }
    stopRequested_.store(false);
    
    QList<acutesim::physics::CornerConfig> runCorners = profile.corners;
    if (runCorners.isEmpty()) {
        runCorners.append(acutesim::physics::CornerConfig::Nominal());
    }
    
    // Transient only runs first corner for now (as per Workspace logic)
    auto corner = runCorners.first();
    sim_.applyCorner(netlist, corner);
    
    emit logMessage(QString("[Worker] Starting Transient Analysis (Stop: %1)...").arg(profile.parameters.value("stopTime", "1ms").toString()));
    
    double stopTime = 0.001; // Default 1ms
    QString stopStr = profile.parameters.value("stopTime", "1ms").toString();
    if (stopStr.endsWith("ms")) stopTime = stopStr.left(stopStr.length()-2).toDouble() * 1e-3;
    else if (stopStr.endsWith("us")) stopTime = stopStr.left(stopStr.length()-2).toDouble() * 1e-6;
    else if (stopStr.endsWith("s")) stopTime = stopStr.left(stopStr.length()-1).toDouble();

    double currentTime = 0.0;
    double timeStep = 1e-5; // Default step
    
    // Explicit Loop
    int stepCount = 0;
    while (currentTime < stopTime && !isStopRequested()) {
        SolverStep result = sim_.stepTransient(netlist, timeStep, currentTime);
        currentTime += timeStep;
        stepCount++;
        
        // Emit Data
        QJsonObject point;
        point["type"] = "TransientPoint";
        point["time"] = currentTime;
        
        QJsonArray voltages;
        for(double v : result.nodeVoltages) voltages.append(v);
        point["voltages"] = voltages;
        
        QJsonObject probeVals;
        for (const auto& probe : netlist.globalBlock.probes) {
             double v_pos = (probe.nodePositive > 0 && probe.nodePositive <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodePositive-1] : 0.0;
             double v_neg = (probe.nodeNegative > 0 && probe.nodeNegative <= (int)result.nodeVoltages.size()) ? result.nodeVoltages[probe.nodeNegative-1] : 0.0;
             probeVals[QString::fromStdString(probe.name)] = v_pos - v_neg;
        }
        point["probes"] = probeVals;
        
        emit resultsReady(point);
        
        // Logging throttle
        if (stepCount % 100 == 0) {
             emit streamMessage(QString("t = %1 us").arg(currentTime * 1e6, 0, 'f', 2));
        }
        
        if (!result.stats.converged) {
            QString reason = result.stats.error_detail.empty() ? "Divergence" : QString::fromStdString(result.stats.error_detail);
            ERC::ViolationType vType = ERC::ViolationType::SOLVER_DIVERGENCE;
            // Map types...
             if (reason.contains("Singular Matrix", Qt::CaseInsensitive)) {
                vType = ERC::ViolationType::SOLVER_SINGULAR_MATRIX;
            }
            
            ERC::Violation v(vType, QString("Transient Failed at t=%1s: %2").arg(currentTime).arg(reason), QPoint(-9999, -9999));
            emit solverViolation(v);
            break;
        }
    }
    
    if (isStopRequested()) {
        emit logMessage("[Worker] Simulation stopped by user.");
    } else {
        emit logMessage("[Worker] Transient Analysis Completed.");
    }
    
    running_ = false;
    emit simulationFinished();
}
