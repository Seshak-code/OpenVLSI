// sim_auto_detector.cpp
// Auto-detection utilities for simulation mode selection.

#include "sim_auto_detector.h"
#include <algorithm>

namespace gui::core {

SimAutoDetector::Recommendation SimAutoDetector::analyze(
    int numNodes, int numMosfets, int numDiodes, int numBJTs)
{
    Recommendation r;
    r.estimatedNodeCount   = numNodes;
    r.estimatedDeviceCount = numMosfets + numDiodes + numBJTs;

    // GPU breakeven: typically 32+ non-linear devices at 64+ nodes
    if (numNodes >= 64 && r.estimatedDeviceCount >= 32) {
        r.mode       = "GPU";
        r.reason     = "Large circuit with many non-linear devices — GPU parallelism beneficial";
        r.confidence = 0.85;
    } else if (numNodes >= 20 && numMosfets >= 8) {
        r.mode       = "TENSOR_SOA_SIMD";
        r.reason     = "Medium VLSI block — SIMD tensor path recommended";
        r.confidence = 0.75;
    } else {
        r.mode       = "LEGACY_AOS";
        r.reason     = "Small circuit — scalar AoS path sufficient";
        r.confidence = 0.90;
    }

    return r;
}

} // namespace gui::core
