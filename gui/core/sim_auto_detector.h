#pragma once
// sim_auto_detector.h
// Auto-detection utilities for simulation mode selection.
// GUI-side only — uses only primitive types (no engine internals).

#include <string>
#include <QString>
#include "engine_api/simulation_context.h"

namespace gui::core {

class SimAutoDetector {
public:
    // Mode as a string to avoid pulling in engine internals.
    // Callers convert to CircuitSim::ExecutionMode inside engine_impl.cpp.
    struct Recommendation {
        std::string mode;          // "GPU" | "TENSOR_SOA_SIMD"
        std::string reason;
        double      confidence        = 0.0;
        int         estimatedNodeCount  = 0;
        int         estimatedDeviceCount = 0;
    };

    // Analyze by primitive counts — stays GUI-layer-safe (no TensorNetlist ref).
    static Recommendation analyze(int numNodes, int numMosfets, int numDiodes, int numBJTs);

    // Context detection from file path
    static acutesim::orchestration::SimulationContext detect(const QString& path);
};

} // namespace gui::core
