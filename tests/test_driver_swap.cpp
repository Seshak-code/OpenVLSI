// ============================================================================
// test_driver_swap.cpp — B2 Driver Swap Compile Test
// ============================================================================
// Proves that ISimulationDriver can be implemented without ANY physics or
// solver headers. If this file compiles, the layering boundary is valid.
// ============================================================================

#include <cstdint>
#include "engine_api/isimulation_driver.h"
#include <iostream>
#include <cassert>

using namespace acutesim::compute::orchestration;

// ── Mock Driver (No Physics) ────────────────────────────────────────────────
// This mock proves that ISimulationDriver can be replaced with an alternative
// implementation (e.g., RemoteGrpcDriver, DockerizedDriver) that has ZERO
// knowledge of CircuitSim, TensorNetlist, or any physics internals.

class MockDriver : public ISimulationDriver {
public:
    JobID submitJob(const SimulationRequestDTO& req) override {
        lastAnalysis_ = req.analysis;
        return nextId_++;
    }

    JobID submitBatchJob(const SimulationBatchRequestDTO& req) override {
        batchRunCount_ = static_cast<int>(req.runs.size());
        return nextId_++;
    }

    JobID submitStatisticalJob(const SimulationRequestDTO& baseReq,
                                const MonteCarloConfigDTO& config) override {
        mcSampleCount_ = config.sampleCount;
        return nextId_++;
    }

    void cancelJob(JobID /*id*/) override { cancelled_ = true; }

    bool supportsConcurrentJobs() const override { return true; }

    TerminalResponseDTO executeCommand(const TerminalCommandDTO& cmd) override {
        TerminalResponseDTO resp;
        resp.success = true;
        resp.output = "MockDriver received: " + cmd.rawCommand;
        return resp;
    }

    // Test accessors
    AnalysisTypeDTO lastAnalysis_ = AnalysisTypeDTO::DC;
    int batchRunCount_ = 0;
    uint32_t mcSampleCount_ = 0;
    bool cancelled_ = false;

private:
    JobID nextId_ = 1;
};

int main() {
    std::cout << "B2 Driver Swap Compile Test\n";
    std::cout << "==========================\n\n";

    MockDriver driver;

    // ── Test 1: Interface Polymorphism ──
    ISimulationDriver* base = &driver;

    SimulationRequestDTO req;
    req.analysis = AnalysisTypeDTO::TRANSIENT;
    req.source.payload = "{}";
    JobID j1 = base->submitJob(req);
    assert(j1 > 0);
    assert(driver.lastAnalysis_ == AnalysisTypeDTO::TRANSIENT);
    std::cout << "  [PASS] submitJob() works through base pointer\n";

    // ── Test 2: Batch submission ──
    SimulationBatchRequestDTO batchReq;
    BatchRunDTO run1, run2;
    run1.runIndex = 0; run2.runIndex = 1;
    batchReq.runs = {run1, run2};
    batchReq.analysis = AnalysisTypeDTO::DC;
    JobID j2 = base->submitBatchJob(batchReq);
    assert(j2 > j1);
    assert(driver.batchRunCount_ == 2);
    std::cout << "  [PASS] submitBatchJob() routes correctly\n";

    // ── Test 3: Monte Carlo ──
    MonteCarloConfigDTO mcConfig;
    mcConfig.sampleCount = 50;
    mcConfig.seed = 12345;
    base->submitStatisticalJob(req, mcConfig);
    assert(driver.mcSampleCount_ == 50);
    std::cout << "  [PASS] submitStatisticalJob() routes correctly\n";

    // ── Test 4: Cancellation ──
    base->cancelJob(j1);
    assert(driver.cancelled_);
    std::cout << "  [PASS] cancelJob() works through base pointer\n";

    // ── Test 5: Terminal command ──
    TerminalCommandDTO cmd;
    cmd.rawCommand = "help";
    auto resp = base->executeCommand(cmd);
    assert(resp.success);
    assert(resp.output.find("MockDriver") != std::string::npos);
    std::cout << "  [PASS] executeCommand() works through base pointer\n";

    // ── Test 6: Callbacks compile ──
    base->onJobCompleted = [](JobID, const SimulationResponseDTO&) {};
    base->onBatchCompleted = [](JobID, const SimulationBatchResponseDTO&) {};
    base->onJobProgress = [](JobID, float, const std::string&) {};
    base->onBatchProgress = [](JobID, uint32_t, uint32_t) {};
    base->onEngineEvent = [](const EngineEventDTO&) {};
    std::cout << "  [PASS] All callbacks compile and assign\n";

    // ── Test 7: Concurrent support query ──
    assert(base->supportsConcurrentJobs() == true);
    std::cout << "  [PASS] supportsConcurrentJobs() polymorphic\n";

    std::cout << "\nAll 7 driver swap tests passed.\n";
    std::cout << "This proves ISimulationDriver can be swapped without physics.\n";
    return 0;
}
