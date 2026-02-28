// ============================================================================
// test_determinism_xclient.cpp — B2 Orchestration Determinism Cross-Test
// ============================================================================
// Objective: Prove that the unified execution path (B1) guarantees bit-exact
// identical outputs regardless of whether the API consumer calls submitJob()
// or submitBatchJob(singleRunBatch). 
// Both paths must route through identical Random Number Generator seeding
// sequences, state initialization, and solver logic inside BatchExecutionEngine.
// ============================================================================

#include "compute/orchestration/driver/local_worker_driver.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>

using namespace acutesim::compute::orchestration;

const std::string TEST_NETLIST = 
    "{\n"
    "    \"numGlobalNodes\": 4,\n"
    "    \"globalBlock\": {\n"
    "        \"voltageSources\": [\n"
    "            {\"nodePositive\": 4, \"nodeNegative\": 0, \"dc_voltage\": 1.8},\n"
    "            {\"nodePositive\": 1, \"nodeNegative\": 0, \"dc_voltage\": 0.0, \"transient_expr\": \"PULSE(0 1.8 0 10p 10p 1n 2n)\"}\n"
    "        ],\n"
    "        \"resistors\": [\n"
    "            {\"name\": \"R1\", \"nodeA\": 1, \"nodeB\": 2, \"resistance\": 1e3},\n"
    "            {\"name\": \"R2\", \"nodeA\": 2, \"nodeB\": 3, \"resistance\": 1e3}\n"
    "        ],\n"
    "        \"capacitors\": [\n"
    "            {\"name\": \"C1\", \"nodeA\": 2, \"nodeB\": 0, \"capacitance\": 1e-12},\n"
    "            {\"name\": \"C2\", \"nodeA\": 3, \"nodeB\": 0, \"capacitance\": 1e-12}\n"
    "        ],\n"
    "        \"probes\": [\n"
    "            {\"name\": \"VOUT\", \"nodePositive\": 3, \"nodeNegative\": 0}\n"
    "        ]\n"
    "    }\n"
    "}\n";

std::atomic<bool> job1_done{false};
std::atomic<bool> batch_done{false};
SimulationResponseDTO res1;
SimulationResponseDTO res2;

#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    std::cout << "B2 Orchestration Determinism Cross-Test\n";
    std::cout << "=======================================\n\n";

    LocalWorkerDriver driver;

    // Callbacks
    driver.onJobCompleted = [&](JobID id, const SimulationResponseDTO& resp) {
        if (id == 1) { res1 = resp; job1_done = true; }
    };

    driver.onBatchCompleted = [&](JobID id, const SimulationBatchResponseDTO& resp) {
        if (id == 2) {
            std::cout << "[TEST] onBatchCompleted called for ID " << id << "\n";
            if (!resp.runResults.empty()) res2 = resp.runResults[0];
            batch_done = true;
        }
    };

    driver.onEngineEvent = [](const EngineEventDTO& ev) {
        if (ev.type == EngineEventDTO::Type::Error) {
            std::cerr << "[Sim Error] " << ev.message << "\n";
        }
    };

    // ── Run 1: Legacy submitJob() ──
    std::cout << "Running Path 1: submitJob()...\n";
    SimulationRequestDTO req1;
    req1.source.payload = TEST_NETLIST;
    req1.analysis = AnalysisTypeDTO::TRANSIENT;
    req1.parameters["tstop"] = "5e-9";
    req1.parameters["tstep"] = "10e-12";
    
    driver.submitJob(req1);

    while (!job1_done) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }

    // ── Run 2: Orchestrated submitBatchJob() ──
    std::cout << "Running Path 2: submitBatchJob()...\n";
    SimulationBatchRequestDTO req2;
    req2.source.payload = TEST_NETLIST;
    req2.analysis = AnalysisTypeDTO::TRANSIENT;
    req2.parameters["tstop"] = "5e-9";
    req2.parameters["tstep"] = "10e-12";
    
    BatchRunDTO run0;
    run0.runIndex = 0;
    req2.runs.push_back(run0);

    // B1 unification fires onBatchCompleted AND onJobCompleted for single-run batches,
    // so batch_done will be tripped by the onJobCompleted unwrapper above.

    driver.submitBatchJob(req2);

    while (!batch_done) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }

    // ── Validation ──
    std::cout << "\nValidating cross-path determinism...\n";

    if (!res1.success || !res2.success) {
        std::cerr << "FAIL: Simulation failed.\n";
        if (!res1.success) std::cerr << " Run 1: " << res1.errorMessage << "\n";
        if (!res2.success) std::cerr << " Run 2: " << res2.errorMessage << "\n";
        return 1;
    }

    if (res1.timeVector.size() != res2.timeVector.size()) {
        std::cerr << "FAIL: Step count mismatch! Run1: " << res1.timeVector.size() 
                  << " Run2: " << res2.timeVector.size() << "\n";
        return 1;
    }

    if (res1.timeVector.empty()) {
        std::cerr << "FAIL: Zero timepoints returned. Simulation silent failure.\n";
        return 1;
    }

    // Bit-exact check on all timepoints and node voltages
    bool mismatch = false;
    for(size_t i = 0; i < res1.timeVector.size(); ++i) {
        double t1 = res1.timeVector[i];
        double t2 = res2.timeVector[i];
        uint64_t bt1 = *reinterpret_cast<uint64_t*>(&t1);
        uint64_t bt2 = *reinterpret_cast<uint64_t*>(&t2);
        
        if (bt1 != bt2) {
            std::cerr << "FAIL: Timepoint mismatch at step " << i << "\n";
            mismatch = true;
            break;
        }

        if (res1.nodeVoltages.size() != res2.nodeVoltages.size()) {
             std::cerr << "FAIL: Node maps count mismatch at step " << i << "\n";
             mismatch = true;
             break;
        }

        for(const auto& [nodeIdx, v1List] : res1.nodeVoltages) {
            double v1 = v1List[i];
            double v2 = res2.nodeVoltages[nodeIdx][i];
            uint64_t bv1 = *reinterpret_cast<uint64_t*>(&v1);
            uint64_t bv2 = *reinterpret_cast<uint64_t*>(&v2);
            
            if (bv1 != bv2) {
                std::cerr << std::fixed << std::setprecision(20);
                std::cerr << "FAIL: Bit-mismatch at T=" << t1 
                          << " Node=" << nodeIdx << "\n"
                          << "  Path 1 (SINGLE): " << v1 << " (" << std::hex << bv1 << std::dec << ")\n"
                          << "  Path 2 (BATCH):  " << v2 << " (" << std::hex << bv2 << std::dec << ")\n";
                mismatch = true;
                break;
            }
        }
        if (mismatch) break;
    }

    if (mismatch) return 1;

    std::cout << "PASS: Path 1 (submitJob) and Path 2 (submitBatchJob) are bit-exact identical.\n";
    std::cout << "Unified Execution Boundary verified.\n";
    return 0;
}
