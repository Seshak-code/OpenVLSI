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
#include "components/veriloga_generator.h"
#include "acutesim_engine/netlist/circuit.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>

using namespace acutesim::compute::orchestration;

// Two-stage RC charging from 0 V (UIC): a waveform that exercises every step.
static std::string testSource() {
    TensorNetlist nl;
    nl.numGlobalNodes = 3;
    nl.addVoltageSource(1, 0, 1.8);
    nl.addResistor("R1", 1, 2, 1e3);
    nl.addCapacitor("C1", 2, 0, 1e-12);
    nl.addResistor("R2", 2, 3, 1e3);
    nl.addCapacitor("C2", 3, 0, 1e-12);
    nl.addProbe("VOUT", 3, 0);
    return VerilogAGenerator::generate(nl);
}

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
    req1.source.format = NetlistFormatDTO::VERILOG_A;
    req1.source.payload = testSource();
    req1.analysis = AnalysisTypeDTO::TRANSIENT;
    req1.parameters["stopTime"] = "5e-9";
    req1.parameters["stepHint"] = "10e-12";
    req1.parameters["uic"] = "1";
    
    driver.submitJob(req1);

    while (!job1_done) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }

    // ── Run 2: Orchestrated submitBatchJob() ──
    std::cout << "Running Path 2: submitBatchJob()...\n";
    SimulationBatchRequestDTO req2;
    req2.source.format = NetlistFormatDTO::VERILOG_A;
    req2.source.payload = testSource();
    req2.analysis = AnalysisTypeDTO::TRANSIENT;
    req2.parameters["stopTime"] = "5e-9";
    req2.parameters["stepHint"] = "10e-12";
    req2.parameters["uic"] = "1";
    
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
        if (!res1.success) std::cerr << " Run 1: " << res1.errorDetail << "\n";
        if (!res2.success) std::cerr << " Run 2: " << res2.errorDetail << "\n";
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
