#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "engine_api/simulation_context.h"
#include "acutesim_engine/infrastructure/netlist_compiler.h"
#include "acutesim_engine/infrastructure/postproc_engine.h"

namespace acutesim::compute::orchestration {

using acutesim::orchestration::SimulationContext;

struct SweepParameter {
    std::string variableName;
    double start  = 0.0;
    double stop   = 0.0;
    double step   = 0.0; // or count if log
    bool isLog    = false;

    std::vector<double> generatePoints() const;
};

struct BatchResult {
    std::string sweepParam;
    std::vector<double> sweepValues;
    // Map of "v(out)" -> WaveFamily (a collection of waveforms)
    std::unordered_map<std::string, acutesim::compute::postproc::WaveFamily> families;
};

/**
 * POD result returned by SolverFn — replaces the old QJsonObject.
 * GUI layer converts this to whatever presentation form it needs.
 */
struct SolverStepResult {
    std::string type;                    // "DC", "AC", "TRANSIENT", etc.
    std::vector<double> voltages;        // Node voltages indexed 0…N-1
    std::vector<double> currents;        // Branch currents indexed 0…M-1
    std::unordered_map<std::string, double> probes; // Named probe values
    bool success = false;
    std::string errorMessage;
};

class BatchRunner {
public:
    BatchRunner() = default;

    // Configure the run
    void setContext(const SimulationContext& ctx);
    void setBaseNetlist(const TensorNetlist& netlist);
    void setSweep(const SweepParameter& sweep);

    using SolverFn = std::function<SolverStepResult(std::shared_ptr<const CompiledTensorBlock>)>;

    BatchResult run(SolverFn solver);

    // Progress callbacks (set before calling run())
    std::function<void(int percent)>      onProgress;
    std::function<void(int step, int total)> onStep;

private:
    SimulationContext ctx_;
    TensorNetlist netlist_;
    SweepParameter sweep_;
};

} // namespace acutesim::compute::orchestration
