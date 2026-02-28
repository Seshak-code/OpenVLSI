#pragma once
#include "simulation_trace.h"
#include "acutesim_engine/physics/circuitsim.h"
#include <QString>
#include <QJsonObject>

namespace AIAPI {

/**
 * Step 3.1: Speculative "What-If" Runner
 * Isolate engine that runs counterfactual simulations to test AI hypotheses.
 */

struct SpeculativeOverrides {
    QJsonObject solverSettings; // e.g. {"solver": "LU", "gmin": 1e-9}
    double timeStepOverride = -1.0; // -1 means keep original
    int maxIterationsOverride = -1;
};

class SpeculativeRunner {
public:
    /**
     * Run a speculative simulation based on a base netlist and a set of overrides.
     * This method is CONST-safe regarding the input netlist (it clones it).
     */
    static SimulationTrace runSpeculative(
        const TensorNetlist& baseNetlist,
        const SpeculativeOverrides& overrides,
        double simulationDuration = 1e-6 // Short burst
    );

private:
    static void applyOverrides(CircuitSim& sim, const SpeculativeOverrides& overrides);
};

} // namespace AIAPI
