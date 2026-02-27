// sim_auto_detector.cpp
// Auto-detection utilities for simulation mode selection.
// Analyzes netlist topology to recommend execution mode (GPU / SIMD / scalar).

#include "sim_auto_detector.h"
#include <algorithm>
#include <cmath>

namespace gui::core {

SimAutoDetector::Recommendation SimAutoDetector::analyze(const TensorNetlist& netlist) {
    Recommendation r;

    const int nodes    = netlist.numGlobalNodes;
    const int mosfets  = static_cast<int>(netlist.globalBlock.mosfets.size());
    const int diodes   = static_cast<int>(netlist.globalBlock.diodes.size());
    const int bjts      = static_cast<int>(netlist.globalBlock.bjts.size());

    const int totalDevices = mosfets + diodes + bjts;

    // Heuristic thresholds for mode selection
    // GPU breakeven: typically 32+ non-linear devices at 64+ nodes
    if (nodes >= 64 && totalDevices >= 32) {
        r.mode    = ExecutionMode::GPU;
        r.reason  = "Large circuit with many non-linear devices — GPU parallelism beneficial";
        r.confidence = 0.85;
    } else if (nodes >= 20 && mosfets >= 8) {
        r.mode    = ExecutionMode::TENSOR_SOA_SIMD;
        r.reason  = "Medium VLSI block — SIMD tensor path recommended";
        r.confidence = 0.75;
    } else {
        r.mode    = ExecutionMode::LEGACY_AOS;
        r.reason  = "Small circuit — scalar AoS path sufficient";
        r.confidence = 0.90;
    }

    r.estimatedNodeCount  = nodes;
    r.estimatedDeviceCount = totalDevices;

    return r;
}

} // namespace gui::core
