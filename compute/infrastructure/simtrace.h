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


struct SolverStats 
{
    int iterations = 0;
    double residual = 0.0;
    bool converged = false;
    std::string error_detail;
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
