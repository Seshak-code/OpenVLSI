#pragma once
#include <vector>
#include <string>
#include <chrono>

/**
 * @file simtrace.h
 * @brief Simulation Trace API - Decoupled data storage for simulation results.
 * 
 * This enables:
 * 1. Headless runs (save trace to disk).
 * 2. GUI Replay (scrub through time).
 * 3. Debug Attribution (view residuals/stats per step).
 */

struct SolverStats {
    int iterations = 0;
    double residual = 0.0;
    bool converged = false;
};

struct SimulationStep {
    double time = 0.0;
    std::vector<double> nodeVoltages;
    SolverStats stats;
};

class SimulationTrace {
public:
    std::string netlistName;
    std::string timestamp;
    std::vector<SimulationStep> steps;

    SimulationTrace() {
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
    const SimulationStep& lastStep() const { return steps.back(); }
};
