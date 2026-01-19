#include "speculative_runner.h"
#include <QDateTime> // For timestamp
#include "../analysis/failure_classifier.h"

namespace AIAPI {

SimulationTrace SpeculativeRunner::runSpeculative(
    const TensorNetlist& baseNetlist,
    const SpeculativeOverrides& overrides,
    double simulationDuration
) {
    // 1. Clone the Netlist (Deep Copy via Copy Constructor)
    TensorNetlist specNetlist = baseNetlist; 
    
    // 2. Initialize a fresh Simulator Engine
    CircuitSim specSim;
    // CircuitSim doesn't have explicit initialize(Netlist).
    // It takes the netlist directly in stepTransient.
    
    // 3. Apply Overrides
    applyOverrides(specSim, overrides);

    // 4. Run Simulation Burst
    SimulationTrace trace;
    trace.traceId = QString("SPEC-%1").arg(QDateTime::currentMSecsSinceEpoch());
    trace.topologyHash = QString::number(12345); // TODO: Calculate actual hash
    trace.timestamp = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    trace.configParams.predictionEnabled = true;

    double currentTime = 0.0;
    double dt = (overrides.timeStepOverride > 0) ? overrides.timeStepOverride : 1e-9;
    
    // Initialize trace containers
    // TODO: Populate Device Mix from netlist

    int steps = 0;
    const int MAX_STEPS = 5000; // Safety cap

    while (currentTime < simulationDuration && steps < MAX_STEPS) {
        SolverStep step = specSim.stepTransient(specNetlist, dt, currentTime);
        
        // Capture Failures via Classifier
        if (!step.stats.converged) {
             // Use public stats members
             Csr_matrix emptyDebugMatrix; 
             SolverResult debugResult = {{}, step.stats.residual, step.stats.iterations, false};

            trace.failureModes = FailureClassifier::analyzeConvergenceFailure(
                emptyDebugMatrix, {}, step.nodeVoltages, 
                debugResult,
                currentTime
            );
            break; 
        }

        currentTime = step.time;
        steps++;
    }

    return trace;
}

void SpeculativeRunner::applyOverrides(CircuitSim& sim, const SpeculativeOverrides& overrides) {
    // TODO: Implement solver switching via CircuitSim API
    if (overrides.solverSettings.contains("gmin")) {
         // sim.setGmin(overrides.solverSettings["gmin"].toDouble());
    }
}

} // namespace AIAPI
