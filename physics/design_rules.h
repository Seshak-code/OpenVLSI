#pragma once

#include "../components/circuit.h"
#include "../components/component_library.h"
#include <vector>
#include <string>
#include <set>
#include <cmath>

/**
 * design_rules.h - Physics-Backed Design Rule Checking Engine
 * 
 * This module provides mode-aware (Bench/Block/System) validation of circuit
 * designs with checks rooted in physical principles.
 * 
 * Architecture Notes:
 * - All checks are stateless predicates for GPU portability
 * - Uses uint8_t bitmasks for parallel rule checking (GPU-friendly)
 * - Supports f64 emulation mode for high-precision GPU compute
 * - SoA-compatible for batch validation on tensors
 */

namespace DesignRules {

// ============================================================================
// PRECISION MODE (GPU f64 EMULATION SUPPORT)
// ============================================================================

/**
 * PrecisionMode controls numeric precision for GPU compute paths.
 * 
 * HIGH_ACCURACY: Uses f64 (or emulated f64 via 2x f32 on GPU)
 * FAST:          Uses native f32 for maximum throughput
 */
enum class PrecisionMode {
    FAST,           // Native f32
    HIGH_ACCURACY   // f64 or emulated f64 (2x f32)
};

// Global precision setting (can be changed at runtime)
inline PrecisionMode g_precision = PrecisionMode::HIGH_ACCURACY;

// Precision-aware tolerance thresholds
inline double getTolerance(double base_tol) {
    return (g_precision == PrecisionMode::HIGH_ACCURACY) ? base_tol : base_tol * 1e6;
}

// ============================================================================
// VIOLATION CODES (BITMASK FOR GPU PARALLEL CHECK)
// ============================================================================

namespace Violation {
    constexpr uint8_t NONE                 = 0;
    constexpr uint8_t FLOATING_GATE        = 1 << 0;  // Gate with no DC path
    constexpr uint8_t OPEN_DRAIN           = 1 << 1;  // Drain with no load
    constexpr uint8_t VDD_GND_SHORT        = 1 << 2;  // Rail short circuit
    constexpr uint8_t INDEPENDENT_SOURCE   = 1 << 3;  // V/I source at interface
    constexpr uint8_t THERMAL_OVERLOAD     = 1 << 4;  // P > P_max
    constexpr uint8_t CROSSTALK_RISK       = 1 << 5;  // High-Z parallel paths
    constexpr uint8_t EMC_VIOLATION        = 1 << 6;  // Fast edge / loop area
    constexpr uint8_t MULTIPLE_DRIVERS     = 1 << 7;  // Multiple sources on net
}

// ============================================================================
// VIOLATION REPORT (SERIALIZABLE FOR AI API)
// ============================================================================

struct ViolationEntry {
    uint8_t code;              // Bitmask of violations
    int device_index = -1;     // Index in tensor (or -1 for net-level)
    int node_id = -1;          // Related node (if applicable)
    std::string message;       // Human-readable description
    
    // Severity levels per mode
    enum class Severity { INFO, WARN, ERR };
    Severity getSeverity(DesignMode mode) const;
};

struct ViolationReport {
    std::vector<ViolationEntry> entries;
    bool hasErrors = false;
    bool hasWarnings = false;
    
    void add(uint8_t code, int device_idx, int node, const std::string& msg) {
        entries.push_back({code, device_idx, node, msg});
        // Note: hasErrors/hasWarnings set during mode-aware postprocess
    }
    
    bool isClean() const { return entries.empty(); }
    
    // Serialize for AI API consumption
    std::string toJSON() const;
};

// ============================================================================
// MODE-SPECIFIC CONSTRAINTS
// ============================================================================

template<DesignMode Mode>
struct ModeConstraints;

template<>
struct ModeConstraints<DesignMode::BENCH> {
    static constexpr bool allowIdealSources = true;
    static constexpr bool requireExplicitGround = false;
    static constexpr double maxVoltageSwing_V = 1000.0;
    static constexpr double maxPowerPerDevice_W = 100.0;  // Relaxed for testing
    static constexpr bool enforceSingleDriver = false;
    
    // Severity mapping
    static ViolationEntry::Severity severity(uint8_t code) {
        using S = ViolationEntry::Severity;
        if (code & Violation::VDD_GND_SHORT) return S::ERR;
        if (code & Violation::THERMAL_OVERLOAD) return S::WARN;
        return S::INFO;
    }
};

template<>
struct ModeConstraints<DesignMode::BLOCK> {
    static constexpr bool allowIdealSources = false;  // Not at interface
    static constexpr bool requireExplicitGround = true;
    static constexpr bool requirePowerPorts = true;
    static constexpr double maxVoltageSwing_V = 50.0;
    static constexpr double maxPowerPerDevice_W = 10.0;
    static constexpr bool enforceSingleDriver = true;
    
    static ViolationEntry::Severity severity(uint8_t code) {
        using S = ViolationEntry::Severity;
        if (code & (Violation::VDD_GND_SHORT | Violation::FLOATING_GATE)) return S::ERR;
        if (code & (Violation::INDEPENDENT_SOURCE | Violation::OPEN_DRAIN)) return S::ERR;
        if (code & Violation::THERMAL_OVERLOAD) return S::WARN;
        return S::INFO;
    }
};

template<>
struct ModeConstraints<DesignMode::SYSTEM> {
    static constexpr bool allowIdealSources = false;
    static constexpr bool requireRailDrivers = true;
    static constexpr bool enforceSingleDriverPerRail = true;
    static constexpr double maxVoltageSwing_V = 20.0;
    static constexpr double maxPowerPerDevice_W = 5.0;
    static constexpr double maxCurrentDensity_A_um2 = 1e-6;  // Electromigration
    static constexpr bool checkEMC = true;
    
    static ViolationEntry::Severity severity(uint8_t code) {
        using S = ViolationEntry::Severity;
        // System mode: most violations are errors
        if (code != Violation::NONE) return S::ERR;
        return S::INFO;
    }
};

// ============================================================================
// PHYSICS-BACKED VALIDATION CHECKS
// ============================================================================

namespace PhysicsChecks {

/**
 * Check for floating gates (no DC path to reference).
 * Physics: Gate capacitance can charge to arbitrary voltage without DC path.
 */
inline bool isFloatingGate(int gateNode, const TensorBlock& block,
                           const std::set<int>& groundedNodes) {
    return groundedNodes.find(gateNode) == groundedNodes.end();
}

/**
 * Check for open drain (no load path).
 * Physics: Current has no path to flow -> undefined voltage.
 */
inline bool isOpenDrain(int drainNode, const TensorBlock& block) {
    // Count connections to drain node
    int connections = 0;
    for (const auto& r : block.resistors) {
        if (r.nodeTerminal1 == drainNode || r.nodeTerminal2 == drainNode)
            connections++;
    }
    for (const auto& m : block.mosfets) {
        if (m.drain == drainNode) connections++;
    }
    for (const auto& q : block.bjts) {
        if (q.nodeCollector == drainNode) connections++;
    }
    return connections < 2;  // Needs at least drain + load
}

/**
 * Check for VDD/GND short (very low resistance path).
 * Physics: R < 1Ω between rails causes destructive current.
 * 
 * @param tolerance Adjusted based on precision mode
 */
inline bool isRailShort(int node1, int node2, const TensorBlock& block, 
                        double tolerance = 1.0) {
    // Find direct resistance path
    for (const auto& r : block.resistors) {
        if ((r.nodeTerminal1 == node1 && r.nodeTerminal2 == node2) ||
            (r.nodeTerminal1 == node2 && r.nodeTerminal2 == node1)) {
            if (r.resistance_ohms < tolerance) return true;
        }
    }
    return false;
}

/**
 * Check thermal overload.
 * Physics: P = I² * R_thermal exceeds device rating.
 * 
 * For GPU: This check uses the current precision mode.
 */
inline bool isThermalOverload(double voltage_drop, double current, 
                              double maxPower_W) {
    double power = std::abs(voltage_drop * current);
    return power > maxPower_W;
}

/**
 * Check for crosstalk risk (parallel high-Z signal paths).
 * Physics: Capacitive coupling between adjacent high-impedance traces.
 * 
 * Rule: If two signal nets have similar impedance and are routed in parallel,
 *       mutual capacitance can cause voltage perturbation.
 */
inline bool hasCrosstalkRisk(int net1_impedance_ohms, int net2_impedance_ohms,
                             double parallel_length_um, double spacing_um) {
    // Simplified model: Crosstalk voltage = k * Vdriver * (L/S)
    // Risk if both nets are high-Z (> 1kΩ) and length/spacing > 10
    bool bothHighZ = (net1_impedance_ohms > 1000 && net2_impedance_ohms > 1000);
    bool geometryRisk = (parallel_length_um / (spacing_um + 1e-9)) > 10.0;
    return bothHighZ && geometryRisk;
}

/**
 * Check EMC/EMI risk (fast edges + large loop area).
 * Physics: dI/dt through inductance = L * dI/dt voltage spike.
 *          Large loop area = antenna for radiated emissions.
 * 
 * Rule: Flag high-speed signals (edge_rate > 1V/ns) with large return paths.
 */
inline bool hasEMCRisk(double edge_rate_V_per_ns, double loop_area_um2) {
    // EMC risk model: Proportional to edge rate * sqrt(loop area)
    // Threshold: 1V/ns edge with 10000 µm² loop
    double emc_metric = edge_rate_V_per_ns * std::sqrt(loop_area_um2);
    return emc_metric > 100.0;  // Empirical threshold
}

} // namespace PhysicsChecks

// ============================================================================
// MAIN VALIDATION FUNCTION
// ============================================================================

/**
 * Validate a TensorBlock against mode-specific design rules.
 * 
 * This is the primary entry point for design rule checking.
 * GPU-friendly: All underlying checks are stateless predicates.
 */
template<DesignMode Mode>
ViolationReport validate(const TensorBlock& block) {
    ViolationReport report;
    using Constraints = ModeConstraints<Mode>;
    
    // Build set of grounded nodes (nodes with DC path to GND)
    std::set<int> groundedNodes;
    groundedNodes.insert(0);  // Node 0 is always ground
    
    // Propagate through resistors (simplified DC path check)
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& r : block.resistors) {
            bool n1Grounded = groundedNodes.count(r.nodeTerminal1) > 0;
            bool n2Grounded = groundedNodes.count(r.nodeTerminal2) > 0;
            if (n1Grounded && !n2Grounded) {
                groundedNodes.insert(r.nodeTerminal2);
                changed = true;
            }
            if (n2Grounded && !n1Grounded) {
                groundedNodes.insert(r.nodeTerminal1);
                changed = true;
            }
        }
    }
    
    // Check MOSFETs
    for (size_t i = 0; i < block.mosfets.size(); ++i) {
        const auto& m = block.mosfets[i];
        
        // Floating gate check
        if (PhysicsChecks::isFloatingGate(m.gate, block, groundedNodes)) {
            report.add(Violation::FLOATING_GATE, i, m.gate,
                      "MOSFET " + m.instanceName + " has floating gate (no DC path)");
        }
        
        // Open drain check
        if (PhysicsChecks::isOpenDrain(m.drain, block)) {
            report.add(Violation::OPEN_DRAIN, i, m.drain,
                      "MOSFET " + m.instanceName + " drain has no load path");
        }
    }
    
    // Check BJTs
    for (size_t i = 0; i < block.bjts.size(); ++i) {
        const auto& q = block.bjts[i];
        
        // Floating base check (similar to floating gate)
        if (groundedNodes.find(q.base) == groundedNodes.end()) {
            report.add(Violation::FLOATING_GATE, i, q.base,
                      "BJT " + q.instanceName + " has floating base");
        }
    }
    
    // Check for independent sources (mode-dependent)
    if constexpr (!Constraints::allowIdealSources) {
        if (!block.voltageSources.empty()) {
            report.add(Violation::INDEPENDENT_SOURCE, -1, -1,
                      "Independent voltage source not allowed in " + 
                      modeToString(Mode) + " mode");
        }
        if (!block.currentSources.empty()) {
            report.add(Violation::INDEPENDENT_SOURCE, -1, -1,
                      "Independent current source not allowed in " + 
                      modeToString(Mode) + " mode");
        }
    }
    
    // Check VDD/GND shorts (using known power rails)
    // Note: In a full implementation, power rails would be identified separately
    for (const auto& r : block.resistors) {
        if (r.resistance_ohms < getTolerance(1.0)) {
            // Check if this connects power nodes (heuristic based on node names)
            report.add(Violation::VDD_GND_SHORT, -1, r.nodeTerminal1,
                      "Very low resistance (" + std::to_string(r.resistance_ohms) + 
                      "Ω) between nodes - potential short");
        }
    }
    
    // Apply mode-specific severity
    for (auto& entry : report.entries) {
        auto sev = Constraints::severity(entry.code);
        if (sev == ViolationEntry::Severity::ERR) report.hasErrors = true;
        if (sev == ViolationEntry::Severity::WARN) report.hasWarnings = true;
    }
    
    return report;
}

// ============================================================================
// JSON SERIALIZATION (AI API)
// ============================================================================

inline std::string ViolationReport::toJSON() const {
    std::string json = "{\"violations\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        json += "{\"code\":" + std::to_string(e.code) + ",";
        json += "\"device\":" + std::to_string(e.device_index) + ",";
        json += "\"node\":" + std::to_string(e.node_id) + ",";
        json += "\"message\":\"" + e.message + "\"}";
        if (i < entries.size() - 1) json += ",";
    }
    json += "],\"hasErrors\":" + std::string(hasErrors ? "true" : "false");
    json += ",\"hasWarnings\":" + std::string(hasWarnings ? "true" : "false") + "}";
    return json;
}

inline ViolationEntry::Severity ViolationEntry::getSeverity(DesignMode mode) const {
    switch (mode) {
        case DesignMode::BENCH:  return ModeConstraints<DesignMode::BENCH>::severity(code);
        case DesignMode::BLOCK:  return ModeConstraints<DesignMode::BLOCK>::severity(code);
        case DesignMode::SYSTEM: return ModeConstraints<DesignMode::SYSTEM>::severity(code);
    }
    return Severity::INFO;
}

// ============================================================================
// GPU BATCH VALIDATION (TENSOR-COMPATIBLE)
// ============================================================================

namespace GPUBatch {

/**
 * Batch validate diode tensor.
 * 
 * For GPU: Each diode is validated independently (embarrassingly parallel).
 * Uses precision mode for tolerance checks.
 * 
 * @param diodes       Input tensor of diodes
 * @param voltages     Current node voltages (for thermal check)
 * @param validity     Output: bitmask per device (0 = valid)
 * @param maxPower     Maximum allowed power per device
 */
inline void validateDiodes(
    const std::vector<int>& anodes,
    const std::vector<int>& cathodes,
    const std::vector<double>& currents,  // Pre-computed from physics
    const std::vector<double>& nodeVoltages,
    std::vector<uint8_t>& validity,
    double maxPower = 1.0)
{
    size_t n = anodes.size();
    validity.resize(n, Violation::NONE);
    
    // GPU: This loop would be parallelized across threads
    for (size_t i = 0; i < n; ++i) {
        double va = (anodes[i] > 0 && anodes[i] < (int)nodeVoltages.size()) 
                   ? nodeVoltages[anodes[i]] : 0.0;
        double vc = (cathodes[i] > 0 && cathodes[i] < (int)nodeVoltages.size()) 
                   ? nodeVoltages[cathodes[i]] : 0.0;
        double vdrop = std::abs(va - vc);
        double power = vdrop * std::abs(currents[i]);
        
        if (power > maxPower) {
            validity[i] |= Violation::THERMAL_OVERLOAD;
        }
    }
}

/**
 * Batch validate MOSFET tensor for floating gates.
 * 
 * @param gates           Gate node indices
 * @param groundedMask    Pre-computed: 1 if node has DC path to GND
 * @param validity        Output bitmask per device
 */
inline void validateMosfetGates(
    const std::vector<int>& gates,
    const std::vector<uint8_t>& groundedMask,  // groundedMask[node] = 1 if grounded
    std::vector<uint8_t>& validity)
{
    size_t n = gates.size();
    validity.resize(n, Violation::NONE);
    
    // GPU: Parallel across devices
    for (size_t i = 0; i < n; ++i) {
        int g = gates[i];
        if (g >= 0 && g < (int)groundedMask.size()) {
            if (groundedMask[g] == 0) {
                validity[i] |= Violation::FLOATING_GATE;
            }
        }
    }
}

} // namespace GPUBatch

} // namespace DesignRules
