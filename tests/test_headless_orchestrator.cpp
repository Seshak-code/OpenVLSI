// ============================================================================
// test_headless_orchestrator.cpp — Headless Smoke Test for ISimulationDriver
// ============================================================================
// This test verifies the Orchestration Layer works WITHOUT any Qt GUI.
// It directly uses LocalWorkerDriver + DTOs, proving the simulation engine
// can run fully headless (cloud, CI, batch, WASM-worker).
//
// ARCHITECTURAL PROOF: If this test compiles and passes, the physics engine
// can be driven without any Qt dependency at the call site.
// ============================================================================

#include <cassert>
#include <iostream>
#include <string>
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>

#include "engine_api/simulation_dto.h"
#include "engine_api/isimulation_driver.h"
#include "../compute/orchestration/driver/local_worker_driver.h"
#include "../components/veriloga_generator.h"

using namespace acutesim::compute::orchestration;

// ── Test Utilities ──────────────────────────────────────────────────────────

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " [" << __FILE__ << ":" << __LINE__ << "]" << std::endl; \
            return 1; \
        } else { \
            std::cout << "PASS: " << msg << std::endl; \
        } \
    } while(0)

// ── Build a simple resistive divider netlist ────────────────────────────────
// VDD (node 1) -- R1 (1kΩ) -- node 2 -- R2 (1kΩ) -- GND (node 0)
// V1 = 5V from node 1 to GND
// Expected: V(node2) ≈ 2.5V (voltage divider)

static SimulationSourceDTO makeResistiveDividerSource() {
    TensorNetlist nl;
    nl.numGlobalNodes = 2;
    nl.addVoltageSource(1, 0, 5.0);
    nl.addResistor("R1", 1, 2, 1000.0);
    nl.addResistor("R2", 2, 0, 1000.0);
    nl.addProbe("Vmid", 2, 0);

    SimulationSourceDTO src;
    src.format  = NetlistFormatDTO::VERILOG_A;
    src.payload = VerilogAGenerator::generate(nl);
    return src;
}

// ── Test 1: DC Analysis via LocalWorkerDriver ───────────────────────────────

int testDCAnalysis() {
    std::cout << "\n=== Test: DC Analysis (Resistive Divider) ===" << std::endl;

    LocalWorkerDriver driver;

    // Blocking synchronization
    std::atomic<bool> done{false};
    SimulationResponseDTO capturedResponse;

    driver.onJobCompleted = [&](JobID id, const SimulationResponseDTO& resp) {
        (void)id;
        capturedResponse = resp;
        done.store(true);
    };

    // Build request
    SimulationRequestDTO req;
    req.source = makeResistiveDividerSource();
    req.analysis = AnalysisTypeDTO::DC;
    req.topologyHash = 0xDEADBEEF;
    req.sessionToken = "test-dc-001";

    // Submit
    JobID jobId = driver.submitJob(req);
    TEST_ASSERT(jobId > 0, "JobID should be positive");

    // Wait for completion (with timeout)
    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            TEST_ASSERT(false, "Timeout waiting for DC analysis");
            return 1;
        }
    }

    // Validate response
    TEST_ASSERT(capturedResponse.success, "DC solve should succeed");
    TEST_ASSERT(capturedResponse.topologyHash == 0xDEADBEEF, "Topology hash should round-trip");
    TEST_ASSERT(!capturedResponse.dcOperatingPoint.empty(), "DC operating point should have values");

    // Check voltage divider: V(node2) ≈ 2.5V
    if (capturedResponse.dcOperatingPoint.size() >= 2) {
        double vMid = capturedResponse.dcOperatingPoint[1]; // node 2 is 0-indexed as [1]
        double error = std::abs(vMid - 2.5);
        std::cout << "  V(node2) = " << vMid << "V (expected 2.5V, error=" << error << ")" << std::endl;
        TEST_ASSERT(error < 0.01, "Voltage divider should give ~2.5V at midpoint");
    }

    // Check convergence telemetry
    TEST_ASSERT(capturedResponse.convergence.iterations > 0, "Should report iterations");
    TEST_ASSERT(capturedResponse.convergence.solveTimeMs > 0, "Should report solve time");

    std::cout << "  Convergence: " << capturedResponse.convergence.iterations << " iterations, "
              << capturedResponse.convergence.solveTimeMs << " ms" << std::endl;

    return 0;
}

// ── Test 2: DTO Purity Check ────────────────────────────────────────────────
// Verify DTOs are default-constructible and have correct defaults

int testDTOPurity() {
    std::cout << "\n=== Test: DTO Purity Check ===" << std::endl;

    SimulationRequestDTO req;
    TEST_ASSERT(req.source.payload.empty(), "Default netlist should be empty");
    TEST_ASSERT(req.analysis == AnalysisTypeDTO::DC, "Default analysis should be DC");
    TEST_ASSERT(req.topologyHash == 0, "Default hash should be 0");
    TEST_ASSERT(req.corner.temperature == 27.0, "Default temp should be 27°C");
    TEST_ASSERT(req.corner.supplyVoltage == 1.8, "Default VDD should be 1.8V");
    TEST_ASSERT(req.corner.processCorner == "TT", "Default corner should be TT");

    SimulationResponseDTO resp;
    TEST_ASSERT(!resp.success, "Default success should be false");
    TEST_ASSERT(resp.errorMessage.empty(), "Default error should be empty");
    TEST_ASSERT(resp.timeVector.empty(), "Default time vector should be empty");
    TEST_ASSERT(resp.nodeVoltages.empty(), "Default node voltages should be empty");
    TEST_ASSERT(resp.convergence.iterations == 0, "Default iterations should be 0");

    ConvergenceTelemetryDTO telem;
    TEST_ASSERT(telem.method == "Newton-Raphson", "Default method should be NR");
    TEST_ASSERT(!telem.gminSteppingUsed, "Default gmin stepping should be false");

    std::cout << "  All DTO defaults verified." << std::endl;
    return 0;
}

// ── Test 3: Analysis Type String Conversion ─────────────────────────────────

int testAnalysisTypeConversion() {
    std::cout << "\n=== Test: Analysis Type Conversion ===" << std::endl;

    TEST_ASSERT(analysisTypeToString(AnalysisTypeDTO::DC) == "DC", "DC -> string");
    TEST_ASSERT(analysisTypeToString(AnalysisTypeDTO::TRANSIENT) == "TRANSIENT", "TRANSIENT -> string");
    TEST_ASSERT(analysisTypeToString(AnalysisTypeDTO::AC) == "AC", "AC -> string");
    TEST_ASSERT(analysisTypeToString(AnalysisTypeDTO::NOISE) == "NOISE", "NOISE -> string");
    TEST_ASSERT(analysisTypeToString(AnalysisTypeDTO::MONTE_CARLO) == "MONTE_CARLO", "MONTE_CARLO -> string");
    TEST_ASSERT(analysisTypeToString(AnalysisTypeDTO::SENSITIVITY) == "SENSITIVITY", "SENSITIVITY -> string");

    return 0;
}

// ── Test 4: Driver Interface Polymorphism ───────────────────────────────────
// Verify that LocalWorkerDriver can be used through ISimulationDriver pointer

int testDriverPolymorphism() {
    std::cout << "\n=== Test: Driver Polymorphism ===" << std::endl;

    std::unique_ptr<ISimulationDriver> driver = std::make_unique<LocalWorkerDriver>();
    TEST_ASSERT(driver != nullptr, "Driver should be constructible via interface");
    TEST_ASSERT(driver->supportsAnalysis(AnalysisTypeDTO::DC), "Should support DC");
    TEST_ASSERT(driver->supportsAnalysis(AnalysisTypeDTO::TRANSIENT), "Should support Transient");
    TEST_ASSERT(driver->supportsAnalysis(AnalysisTypeDTO::AC), "Should support AC");
    TEST_ASSERT(!driver->supportsConcurrentJobs(), "LocalWorker should not support concurrent jobs");

    return 0;
}

// ── Test 5: Batch Analysis via LocalWorkerDriver ────────────────────────────

int testBatchAnalysis() {
    std::cout << "\n=== Test: Batch Analysis (Resistor Sweep) ===" << std::endl;

    LocalWorkerDriver driver;
    std::atomic<bool> done{false};
    SimulationBatchResponseDTO capturedResponse;

    driver.onBatchCompleted = [&](JobID id, const SimulationBatchResponseDTO& resp) {
        capturedResponse = resp;
        done.store(true);
    };

    driver.onBatchProgress = [](JobID, uint32_t completed, uint32_t total) {
        std::cout << "  Batch progress: " << completed << "/" << total << std::endl;
    };

    SimulationBatchRequestDTO batchReq;
    batchReq.source = makeResistiveDividerSource();
    batchReq.analysis = AnalysisTypeDTO::DC;
    
    // Sweep R1 from 1k to 10k in 10 steps
    for (int i = 0; i < 10; ++i) {
        BatchRunDTO run;
        run.runIndex = i;
        ParameterVariationDTO var;
        var.componentName = "R1";
        var.propertyName = "resistance";
        var.overrideValue = 1000.0 + i * 1000.0;
        run.overrides.push_back(var);
        batchReq.runs.push_back(run);
    }

    JobID jobId = driver.submitBatchJob(batchReq);
    TEST_ASSERT(jobId > 0, "Batch JobID should be positive");

    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            TEST_ASSERT(false, "Timeout waiting for batch analysis");
            return 1;
        }
    }

    TEST_ASSERT(!capturedResponse.hadFailures, "Batch should not have failures");
    TEST_ASSERT(capturedResponse.runResults.size() == 10, "Batch should return 10 results");
    
    // As R1 increases, V(node2) should decrease since Vmid = 5 * R2 / (R1 + R2)
    // Runs: R1 = 1k, 2k, 3k... 10k
    // Vmid  = 2.5V, 1.66V, 1.25V... 0.45V
    if (capturedResponse.runResults.size() == 10) {
        double lastVoltage = 5.0; // Starts higher
        for(size_t i = 0; i < capturedResponse.runResults.size(); ++i) {
            const auto& res = capturedResponse.runResults[i];
            if (res.dcOperatingPoint.size() >= 2) {
                double vMid = res.dcOperatingPoint[1];
                TEST_ASSERT(vMid < lastVoltage, "Voltage should decrease as R1 increases");
                lastVoltage = vMid;
            } else {
                TEST_ASSERT(false, "Missing DC operating point for a sweep step");
            }
        }
    }

    return 0;
}

// ── Test 6: Error Handling (Empty Netlist) ──────────────────────────────────

int testErrorHandling() {
    std::cout << "\n=== Test: Error Handling (Empty Netlist) ===" << std::endl;

    LocalWorkerDriver driver;
    std::atomic<bool> done{false};
    SimulationResponseDTO capturedResponse;

    driver.onJobCompleted = [&](JobID, const SimulationResponseDTO& resp) {
        capturedResponse = resp;
        done.store(true);
    };

    SimulationRequestDTO req;
    // source left empty — tests empty-netlist error handling
    req.analysis = AnalysisTypeDTO::DC;

    driver.submitJob(req);

    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() > 10) break;
    }

    TEST_ASSERT(done.load(), "Should complete even with empty netlist");
    // Either it succeeds with trivial result or fails gracefully
    std::cout << "  Result: success=" << capturedResponse.success
              << ", error=" << capturedResponse.errorMessage << std::endl;

    return 0;
}

// ── Test 7: Sweep via BatchExecutionEngine ───────────────────────────────────
// 10-step R1 sweep (1kΩ → 10kΩ): Vmid should decrease monotonically.
// Verifies: correct result count, correct runIndex values, monotonic voltage.

int testSweepViaBatchEngine() {
    std::cout << "\n=== Test: Sweep via BatchExecutionEngine ===" << std::endl;

    LocalWorkerDriver driver;
    std::atomic<bool> done{false};
    SimulationBatchResponseDTO capturedResponse;

    driver.onBatchCompleted = [&](JobID, const SimulationBatchResponseDTO& resp) {
        capturedResponse = resp;
        done.store(true);
    };

    SimulationBatchRequestDTO batchReq;
    batchReq.source = makeResistiveDividerSource();
    batchReq.analysis = AnalysisTypeDTO::DC;

    for (int i = 0; i < 10; ++i) {
        BatchRunDTO run;
        run.runIndex = static_cast<uint32_t>(i);
        ParameterVariationDTO var;
        var.componentName = "R1";
        var.propertyName  = "resistance";
        var.overrideValue = 1000.0 + i * 1000.0;  // 1k, 2k, ..., 10k
        run.overrides.push_back(var);
        batchReq.runs.push_back(run);
    }

    driver.submitBatchJob(batchReq);

    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() > 15) {
            TEST_ASSERT(false, "Timeout waiting for sweep batch");
            return 1;
        }
    }

    TEST_ASSERT(capturedResponse.runResults.size() == 10, "Sweep should return exactly 10 results");
    TEST_ASSERT(!capturedResponse.hadFailures, "Sweep should have no failures");

    // Verify runIndex values are 0..9 in order
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT(capturedResponse.runResults[i].runIndex == i,
            "Run index should match sweep step");
    }

    // Vmid = 5 * R2 / (R1 + R2); as R1 increases, Vmid decreases
    double lastVoltage = 5.0;
    for (int i = 0; i < 10; ++i) {
        const auto& res = capturedResponse.runResults[i];
        TEST_ASSERT(res.success, "Each sweep run should succeed");
        if (res.dcOperatingPoint.size() >= 2) {
            double vMid = res.dcOperatingPoint[1];
            std::cout << "  Step " << i << ": R1=" << (1000 + i*1000)
                      << " Ω  Vmid=" << vMid << " V" << std::endl;
            TEST_ASSERT(vMid < lastVoltage, "Vmid should decrease as R1 increases");
            lastVoltage = vMid;
        } else {
            TEST_ASSERT(false, "Missing DC operating point for sweep step");
        }
    }

    return 0;
}

// ── Test 8: Monte Carlo via BatchExecutionEngine ─────────────────────────────
// 100-run MC on R1 with 5% Gaussian tolerance.
// Verifies: result count, mean ≈ 2.5V, stddev > 0, runs are not all identical.

int testMonteCarloViaBatchEngine() {
    std::cout << "\n=== Test: Monte Carlo via BatchExecutionEngine ===" << std::endl;

    LocalWorkerDriver driver;
    std::atomic<bool> done{false};
    SimulationBatchResponseDTO capturedResponse;

    driver.onBatchCompleted = [&](JobID, const SimulationBatchResponseDTO& resp) {
        capturedResponse = resp;
        done.store(true);
    };

    SimulationRequestDTO baseReq;
    baseReq.source = makeResistiveDividerSource();
    baseReq.analysis     = AnalysisTypeDTO::DC;
    baseReq.sessionToken = "mc-test-001";

    MonteCarloConfigDTO mcConfig;
    mcConfig.sampleCount = 100;
    mcConfig.seed        = 42;

    MonteCarloVariationDTO v;
    v.componentName    = "R1";
    v.propertyName     = "resistance";
    v.distributionType = "Gaussian";
    v.nominalValue     = 1000.0;
    v.tolerance        = 0.05;  // 5%
    mcConfig.variations.push_back(v);

    driver.submitStatisticalJob(baseReq, mcConfig);

    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() > 30) {
            TEST_ASSERT(false, "Timeout waiting for MC job");
            return 1;
        }
    }

    TEST_ASSERT(capturedResponse.runResults.size() == 100, "MC should return 100 results");

    int successCount = 0;
    for (const auto& run : capturedResponse.runResults) {
        if (run.success) ++successCount;
    }
    TEST_ASSERT(successCount == 100, "All 100 MC runs should succeed");

    // Statistical summary must be populated
    TEST_ASSERT(!capturedResponse.statistics.mean.empty(), "MC statistics should be populated");
    TEST_ASSERT(capturedResponse.statistics.mean.count("Vmid") > 0,
        "Vmid probe should appear in MC statistics");

    double mean   = capturedResponse.statistics.mean.at("Vmid");
    double stddev = capturedResponse.statistics.stddev.at("Vmid");

    std::cout << "  Vmid: mean=" << mean << " V  stddev=" << stddev << " V" << std::endl;
    TEST_ASSERT(std::abs(mean - 2.5) < 0.1, "MC mean Vmid should be close to 2.5V");
    TEST_ASSERT(stddev > 0.0, "MC stddev should be > 0 (variation happened)");

    // Verify runs are not all identical
    double firstVmid = capturedResponse.runResults[0].probeValues.count("Vmid")
        ? capturedResponse.runResults[0].probeValues.at("Vmid")[0] : 0.0;
    bool anyDiffers = false;
    for (size_t i = 1; i < capturedResponse.runResults.size(); ++i) {
        auto it = capturedResponse.runResults[i].probeValues.find("Vmid");
        if (it != capturedResponse.runResults[i].probeValues.end() && !it->second.empty()) {
            if (std::abs(it->second[0] - firstVmid) > 1e-10) {
                anyDiffers = true;
                break;
            }
        }
    }
    TEST_ASSERT(anyDiffers, "Individual MC run voltages should differ (RNG variation)");

    return 0;
}

// ── Test 9: Monte Carlo Deterministic Seed ──────────────────────────────────
// Same seed produces bit-identical results; different seed produces different results.

static SimulationBatchResponseDTO runMCWithSeed(uint64_t seed) {
    LocalWorkerDriver driver;
    std::atomic<bool> done{false};
    SimulationBatchResponseDTO capturedResponse;

    driver.onBatchCompleted = [&](JobID, const SimulationBatchResponseDTO& resp) {
        capturedResponse = resp;
        done.store(true);
    };

    SimulationRequestDTO baseReq;
    baseReq.source = makeResistiveDividerSource();
    baseReq.analysis     = AnalysisTypeDTO::DC;
    baseReq.sessionToken = "seed-test";

    MonteCarloConfigDTO mcConfig;
    mcConfig.sampleCount = 20;
    mcConfig.seed        = seed;

    MonteCarloVariationDTO v;
    v.componentName    = "R1";
    v.propertyName     = "resistance";
    v.distributionType = "Gaussian";
    v.nominalValue     = 1000.0;
    v.tolerance        = 0.10;
    mcConfig.variations.push_back(v);

    driver.submitStatisticalJob(baseReq, mcConfig);

    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() > 30) break;
    }

    return capturedResponse;
}

int testMonteCarloDeterministicSeed() {
    std::cout << "\n=== Test: Monte Carlo Deterministic Seed ===" << std::endl;

    auto resp1 = runMCWithSeed(42);
    auto resp2 = runMCWithSeed(42);
    auto resp3 = runMCWithSeed(99);

    TEST_ASSERT(resp1.runResults.size() == 20, "seed=42 run 1 should return 20 results");
    TEST_ASSERT(resp2.runResults.size() == 20, "seed=42 run 2 should return 20 results");
    TEST_ASSERT(resp3.runResults.size() == 20, "seed=99 should return 20 results");

    // seed=42 twice → bit-identical Vmid values
    bool allSame42 = true;
    for (size_t i = 0; i < resp1.runResults.size(); ++i) {
        auto it1 = resp1.runResults[i].probeValues.find("Vmid");
        auto it2 = resp2.runResults[i].probeValues.find("Vmid");
        if (it1 == resp1.runResults[i].probeValues.end() ||
            it2 == resp2.runResults[i].probeValues.end()) continue;
        if (it1->second.empty() || it2->second.empty()) continue;
        if (it1->second[0] != it2->second[0]) {
            allSame42 = false;
            break;
        }
    }
    TEST_ASSERT(allSame42, "Same seed=42 should produce bit-identical MC results");

    // seed=42 vs seed=99 → at least one run differs
    bool anyDiffers = false;
    for (size_t i = 0; i < resp1.runResults.size() && i < resp3.runResults.size(); ++i) {
        auto it1 = resp1.runResults[i].probeValues.find("Vmid");
        auto it3 = resp3.runResults[i].probeValues.find("Vmid");
        if (it1 == resp1.runResults[i].probeValues.end() ||
            it3 == resp3.runResults[i].probeValues.end()) continue;
        if (it1->second.empty() || it3->second.empty()) continue;
        if (std::abs(it1->second[0] - it3->second[0]) > 1e-10) {
            anyDiffers = true;
            break;
        }
    }
    TEST_ASSERT(anyDiffers, "Different seeds should produce different MC results");

    std::cout << "  Seed reproducibility verified." << std::endl;
    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Headless Orchestrator Test Suite" << std::endl;
    std::cout << "  (No Qt GUI required)" << std::endl;
    std::cout << "========================================" << std::endl;

    int failures = 0;
    failures += testDTOPurity();
    failures += testAnalysisTypeConversion();
    failures += testDriverPolymorphism();
    failures += testDCAnalysis();
    failures += testBatchAnalysis();
    failures += testErrorHandling();
    failures += testSweepViaBatchEngine();
    failures += testMonteCarloViaBatchEngine();
    failures += testMonteCarloDeterministicSeed();

    std::cout << "\n========================================" << std::endl;
    if (failures == 0) {
        std::cout << "  ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  " << failures << " TEST(S) FAILED" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return failures;
}
