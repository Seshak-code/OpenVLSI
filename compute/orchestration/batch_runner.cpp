#include "batch_runner.h"
#include <cmath>

namespace acutesim::compute::orchestration {

std::vector<double> SweepParameter::generatePoints() const {
    std::vector<double> pts;
    if (step == 0) return pts;

    if (isLog) {
        int points = static_cast<int>(step);
        if (points < 2) points = 2;
        double logStart = std::log10(start);
        double logStop  = std::log10(stop);
        double d = (logStop - logStart) / (points - 1);
        for (int i = 0; i < points; ++i) {
            pts.push_back(std::pow(10.0, logStart + i * d));
        }
    } else {
        if (start < stop && step > 0) {
            for (double v = start; v <= stop + 1e-9; v += step) pts.push_back(v);
        } else if (start > stop && step < 0) {
            for (double v = start; v >= stop - 1e-9; v += step) pts.push_back(v);
        } else {
            pts.push_back(start);
        }
    }
    return pts;
}

void BatchRunner::setContext(const SimulationContext& ctx) { ctx_ = ctx; }
void BatchRunner::setBaseNetlist(const TensorNetlist& netlist) { netlist_ = netlist; }
void BatchRunner::setSweep(const SweepParameter& sweep) { sweep_ = sweep; }

BatchResult BatchRunner::run(SolverFn solver) {
    BatchResult batchRes;
    batchRes.sweepParam  = sweep_.variableName;
    batchRes.sweepValues = sweep_.generatePoints();

    int total = static_cast<int>(batchRes.sweepValues.size());
    if (total == 0) return batchRes;

    // Internal accumulator: variable name → per-step scalar values
    std::unordered_map<std::string, std::vector<double>> scalarResults;

    for (int i = 0; i < total; ++i) {
        double val = batchRes.sweepValues[i];

        // 1. Update Context (overwrite expression to numeric string)
        ctx_.variableExprs[sweep_.variableName] = std::to_string(val);
        ctx_.resolveVariables();

        // 2. Compile
        auto block = NetlistCompiler::compile(netlist_, ctx_);

        // 3. Run Solver
        SolverStepResult stepRes = solver(block);

        // 4. Aggregate
        if (stepRes.type == "DC") {
            for (size_t n = 0; n < stepRes.voltages.size(); ++n) {
                std::string key = "v(node" + std::to_string(n + 1) + ")";
                scalarResults[key].push_back(stepRes.voltages[n]);
            }
            for (const auto& [k, v] : stepRes.probes) {
                std::string key = "v(\"" + k + "\")";
                scalarResults[key].push_back(v);
            }
        }

        if (onStep)     onStep(i + 1, total);
        if (onProgress) onProgress((i + 1) * 100 / total);
    }

    // Convert collected scalars to families
    for (const auto& [k, vals] : scalarResults) {
        acutesim::compute::postproc::Waveform w;
        w.name   = k;
        w.unit   = "V";
        w.time   = batchRes.sweepValues; // X-axis is the sweep parameter
        w.values = vals;

        acutesim::compute::postproc::WaveFamily fam;
        fam.sweepParam  = sweep_.variableName;
        fam.sweepValues = batchRes.sweepValues;
        fam.members.push_back(w);

        batchRes.families[k] = std::move(fam);
    }

    return batchRes;
}

} // namespace acutesim::compute::orchestration
