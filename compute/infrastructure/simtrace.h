#pragma once
#include <vector>
#include <string>
#include <chrono>

/*
 * simtrace.h
 * Solver Trace API - Decoupled data storage for simulation results.
 *
 * This enables:
 * 1. Headless runs (save trace to disk).
 * 2. GUI Replay (scrub through time).
 * 3. Debug Attribution (view residuals/stats per step).
 *
 * NOTE: This is the SOLVER-SIDE trace. For AI/RAG traces, see ai_api/simulation_trace.h.
 */

// ============================================================================
// NR CONVERGENCE DIAGNOSTICS
// ============================================================================

struct ResidualContribution {
    std::string deviceName;     // "D1", "M1"
    std::string deviceType;     // "Diode", "MOSFET"
    int nodeIndex = 0;          // 1-based node affected
    double currentContribution = 0.0; // Amperes
    double percentOfTotal = 0.0;      // % of max KCL error
};

struct ConvergenceStep {
    int iteration = 0;
    double residualNorm = 0.0;      // Max KCL violation [A]
    double maxVoltageDelta = 0.0;   // Max |v_new - v_old| [V]
    double dampingFactor = 1.0;     // 1.0 = full step, 0.5 = damped
    int worstNode = 0;              // 1-based
};

enum class ConvergenceFailureType {
    NONE,
    SINGULAR_MATRIX,
    DIVERGENCE,
    LIMIT_CYCLE,
    SLOW_CONVERGENCE,
    FLOATING_NODE_INDUCED,
    EXPONENTIAL_OVERFLOW
};

inline std::string convergenceFailureToString(ConvergenceFailureType t) {
    switch (t) {
        case ConvergenceFailureType::NONE: return "None";
        case ConvergenceFailureType::SINGULAR_MATRIX: return "Singular Matrix";
        case ConvergenceFailureType::DIVERGENCE: return "Divergence";
        case ConvergenceFailureType::LIMIT_CYCLE: return "Limit Cycle";
        case ConvergenceFailureType::SLOW_CONVERGENCE: return "Slow Convergence";
        case ConvergenceFailureType::FLOATING_NODE_INDUCED: return "Floating Node Induced";
        case ConvergenceFailureType::EXPONENTIAL_OVERFLOW: return "Exponential Overflow";
        default: return "Unknown";
    }
}

// ============================================================================
// SOLVER STATS
// ============================================================================

struct SolverStats
{
    int iterations = 0;
    double residual = 0.0;
    bool converged = false;
    std::string error_detail;

    // Phase 1.7.9: Trust Metadata
    std::string method = "Newton-Raphson";     // Solver method used
    std::string integrationMethod;              // "Trapezoidal", "Gear2", "" for DC
    bool gminSteppingUsed = false;
    bool sourceSteppingUsed = false;
    double convergenceTolerance = 1e-6;
    double solveTimeMs = 0.0;

    // NR Convergence Diagnostics
    ConvergenceFailureType failureType = ConvergenceFailureType::NONE;
    std::vector<ConvergenceStep> convergenceHistory;
    std::vector<ResidualContribution> worstDevices;
    std::string convergenceJustification;

    std::string provenance() const {
        std::string p = "Converged in " + std::to_string(iterations) + " iterations using " + method;
        if (!integrationMethod.empty()) {
            p += " (" + integrationMethod + ")";
        }
        p += ". Max KCL residual: " + std::to_string(residual) + " A.";
        if (gminSteppingUsed) p += " GMIN stepping applied.";
        if (sourceSteppingUsed) p += " Source stepping applied.";
        if (failureType != ConvergenceFailureType::NONE) {
            p += " Failure: " + convergenceFailureToString(failureType) + ".";
        }
        return p;
    }
};

struct SolverStep 
{
    double time = 0.0;
    std::vector<double> nodeVoltages;
    SolverStats stats;
    uint64_t topologyHash = 0;
};

class SolverTrace 
{
public:
    std::string netlistName;
    std::string timestamp;
    std::vector<SolverStep> steps;

    SolverTrace() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        timestamp = std::ctime(&in_time_t);
    }

    void addStep(double t, const std::vector<double>& voltages, const SolverStats& stats) {
        steps.push_back({t, voltages, stats});
    }

    void clear() {
        steps.clear();
    }

    size_t size() const { return steps.size(); }
    const SolverStep& lastStep() const { return steps.back(); }
};
