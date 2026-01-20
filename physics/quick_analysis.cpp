#include "quick_analysis.h"
#include <QElapsedTimer>
#include <cmath>

// Static member initialization
QMap<int, double> QuickAnalysis::cachedEstimate_;
size_t QuickAnalysis::cachedHash_ = 0;
bool QuickAnalysis::isStale_ = true;

QMap<int, double> QuickAnalysis::estimateDC(const TensorNetlist& net) {
    QMap<int, double> result;
    
    // Start timeout timer
    QElapsedTimer timer;
    timer.start();
    
    const int TIMEOUT_MS = 100;
    const int numNodes = net.numGlobalNodes;
    
    if (numNodes <= 0) {
        return result;
    }
    
    // Simple linearized estimate using source superposition
    // This is a quick approximation, not a full Newton solve
    
    // Initialize all nodes to 0V
    std::vector<double> voltages(numNodes, 0.0);
    
    // Access components through globalBlock
    const TensorBlock& block = net.globalBlock;
    
    // Apply voltage sources directly (they set node voltages)
    for (const auto& vs : block.voltageSources) {
        if (vs.nodePositive > 0 && static_cast<size_t>(vs.nodePositive) <= voltages.size()) {
            voltages[vs.nodePositive - 1] = vs.voltage_V;
        }
    }
    
    // For each current source, estimate based on load resistance
    // (Very rough: assume 1k default load)
    for (const auto& cs : block.currentSources) {
        if (cs.nodePositive > 0 && static_cast<size_t>(cs.nodePositive) <= voltages.size()) {
            // I * R estimate (assume 1k load)
            voltages[cs.nodePositive - 1] += cs.current_A * 1000.0;
        }
    }
    
    // Check timeout
    if (timer.elapsed() > TIMEOUT_MS) {
        return result; // Return empty on timeout
    }
    
    // Build result map
    for (int i = 0; i < numNodes; ++i) {
        result[i + 1] = voltages[i]; // 1-indexed nodes
    }
    
    // Cache result using block's topology hash
    cachedEstimate_ = result;
    cachedHash_ = block.topologyHash;
    isStale_ = false;
    
    return result;
}

bool QuickAnalysis::isValid(const QMap<int, double>& estimate) {
    if (estimate.isEmpty()) {
        return false;
    }
    
    // Check for unreasonable values (>1000V or NaN)
    for (auto it = estimate.begin(); it != estimate.end(); ++it) {
        double v = it.value();
        if (std::isnan(v) || std::isinf(v) || std::abs(v) > 1000.0) {
            return false;
        }
    }
    
    return true;
}

QMap<int, double> QuickAnalysis::getCached(size_t currentHash) {
    if (!isStale_ && cachedHash_ == currentHash) {
        return cachedEstimate_;
    }
    return QMap<int, double>(); // Empty if stale or hash mismatch
}

void QuickAnalysis::invalidate() {
    isStale_ = true;
    cachedEstimate_.clear();
}
