#pragma once
// sim_auto_detector.h
// Auto-detection utilities for simulation mode selection.

#include "../../acutesim_engine/physics/circuitsim.h"   // ExecutionMode
#include "../../acutesim_engine/netlist/circuit.h"        // TensorNetlist

namespace gui::core {

class SimAutoDetector {
public:
    struct Recommendation {
        CircuitSim::ExecutionMode mode    = CircuitSim::ExecutionMode::LEGACY_AOS;
        std::string               reason;
        double                    confidence        = 0.0;
        int                       estimatedNodeCount  = 0;
        int                       estimatedDeviceCount = 0;
    };

    // Analyze netlist topology and recommend an execution mode
    static Recommendation analyze(const TensorNetlist& netlist);
};

} // namespace gui::core
