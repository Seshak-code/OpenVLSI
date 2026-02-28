#pragma once

/**
 * test_simulation_suite.h
 * 
 * Automated tests for Phase 1.7 Simulation Suite
 * Tests: Profiles, Explorer, QuickAnalysis, Workspace flows
 */

#include <iostream>
#include <cassert>
#include <string>
#include <unordered_map>
#include "../gui/analysis/simulation_profile.h"
#include "../physics/quick_analysis.h"
#include "acutesim_engine/netlist/circuit.h"

namespace SimulationTests {

// ============================================================================
// PROFILE TESTS
// ============================================================================

inline bool test_profile_creation() {
    std::cout << "  [Profile Creation] ";
    
    auto quickDC = Simulation::SimulationProfile::quickDC();
    assert(quickDC.profileId == "quick_dc");
    assert(quickDC.name == "Quick DC");
    assert(quickDC.analysis == Simulation::AnalysisType::DC);
    assert(quickDC.category == Simulation::SimulationProfile::Category::QUICK);
    
    auto transient = Simulation::SimulationProfile::transient();
    assert(transient.profileId == "transient_default");
    assert(transient.analysis == Simulation::AnalysisType::TRANSIENT);
    
    auto ac = Simulation::SimulationProfile::acSweep();
    assert(ac.requiresDC == true);  // AC requires DC operating point
    assert(ac.analysis == Simulation::AnalysisType::AC);
    
    std::cout << "PASS\n";
    return true;
}

inline bool test_profile_status_transitions() {
    std::cout << "  [Profile Status Transitions] ";
    
    Simulation::SimulationProfile profile = Simulation::SimulationProfile::quickDC();
    
    // Initial state
    assert(profile.status == Simulation::SimulationProfile::Status::READY);
    
    // Simulate run
    profile.status = Simulation::SimulationProfile::Status::RUNNING;
    assert(profile.status == Simulation::SimulationProfile::Status::RUNNING);
    
    // Success
    profile.status = Simulation::SimulationProfile::Status::PASSED;
    assert(profile.status == Simulation::SimulationProfile::Status::PASSED);
    
    // Topology change -> STALE
    profile.status = Simulation::SimulationProfile::Status::STALE;
    assert(profile.status == Simulation::SimulationProfile::Status::STALE);
    
    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// SIMULATION RUN TESTS
// ============================================================================

inline bool test_run_creation() {
    std::cout << "  [Run Creation] ";
    
    Simulation::SimulationRun run;
    run.runId = "test123";
    run.profileId = "quick_dc";
    run.topologyHash = 0xABCD1234;
    run.status = Simulation::SimulationRun::Status::PENDING;
    assert(run.runId == "test123");
    assert(run.status == Simulation::SimulationRun::Status::PENDING);
    
    std::cout << "PASS\n";
    return true;
}

inline bool test_run_summary() {
    std::cout << "  [Run Summary Generation] ";
    
    Simulation::SimulationRun run;
    run.runId = "abc12345";
    run.status = Simulation::SimulationRun::Status::CONVERGED;
    run.iterations = 5;
    run.solveTime_ms = 12.5;
    
    QString summary = run.toSummary();
    assert(summary.contains("abc12345"));
    assert(summary.contains("PASS"));
    assert(summary.contains("5 iterations"));
    
    // Test failure case
    run.status = Simulation::SimulationRun::Status::FAILED;
    run.failureReason = "Matrix singular";
    summary = run.toSummary();
    assert(summary.contains("FAIL"));
    assert(summary.contains("Matrix singular"));
    
    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// CORNER CONFIG TESTS
// ============================================================================

inline bool test_corner_config() {
    std::cout << "  [Corner Config] ";
    
    auto typical = acutesim::physics::CornerConfig::Nominal();
    assert(typical.temperatureC == 27.0);
    assert(typical.voltageScale == 1.0);
    assert(typical.name == "Nominal");
    
    auto fast = acutesim::physics::CornerConfig::WorstCaseCold();
    assert(fast.temperatureC == -40.0);
    assert(fast.voltageScale == 1.1);
    assert(fast.name == "Fast-Cold");
    
    auto slow = acutesim::physics::CornerConfig::WorstCaseHot();
    assert(slow.temperatureC == 125.0);
    assert(slow.name == "Slow-Hot");
    
    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// QUICK ANALYSIS TESTS
// ============================================================================

inline bool test_quick_analysis_empty_netlist() {
    std::cout << "  [QuickAnalysis Empty Netlist] ";

    TensorNetlist emptyNet;
    emptyNet.numGlobalNodes = 0;

    auto result = QuickAnalysis::estimateDC(emptyNet);
    assert(result.empty());

    std::cout << "PASS\n";
    return true;
}

inline bool test_quick_analysis_simple_circuit() {
    std::cout << "  [QuickAnalysis Simple Circuit] ";

    TensorNetlist net;
    net.numGlobalNodes = 3;

    // Add a 5V source to node 1
    net.addVoltageSource(1, 0, 5.0);

    auto result = QuickAnalysis::estimateDC(net);

    // Should have estimate for node 1
    assert(result.count(1) > 0);
    assert(std::abs(result.at(1) - 5.0) < 0.001);  // V(1) ≈ 5V

    assert(QuickAnalysis::isValid(result));

    std::cout << "PASS\n";
    return true;
}

inline bool test_quick_analysis_caching() {
    std::cout << "  [QuickAnalysis Caching] ";

    // Clear cache first
    QuickAnalysis::invalidate();

    TensorNetlist net;
    net.numGlobalNodes = 2;
    net.addVoltageSource(1, 0, 3.3);

    // First call - computes
    auto result1 = QuickAnalysis::estimateDC(net);
    size_t hash = net.globalBlock.topologyHash;

    // Get cached - should match
    auto cached = QuickAnalysis::getCached(hash);
    assert(!cached.empty());
    assert(cached == result1);

    // After invalidate, cache should be empty
    QuickAnalysis::invalidate();
    cached = QuickAnalysis::getCached(hash);
    assert(cached.empty());

    std::cout << "PASS\n";
    return true;
}

inline bool test_quick_analysis_validation() {
    std::cout << "  [QuickAnalysis Validation] ";

    // Valid result
    std::unordered_map<int, double> valid;
    valid[1] = 3.3;
    valid[2] = 1.8;
    assert(QuickAnalysis::isValid(valid));

    // Empty is invalid
    std::unordered_map<int, double> empty;
    assert(!QuickAnalysis::isValid(empty));

    // Extreme value is invalid
    std::unordered_map<int, double> extreme;
    extreme[1] = 2000.0;  // > 1000V
    assert(!QuickAnalysis::isValid(extreme));

    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// ANALYSIS TYPE STRING CONVERSION
// ============================================================================

inline bool test_analysis_type_string() {
    std::cout << "  [Analysis Type Strings] ";
    
    assert(Simulation::analysisToString(Simulation::AnalysisType::DC) == "DC");
    assert(Simulation::analysisToString(Simulation::AnalysisType::AC) == "AC");
    assert(Simulation::analysisToString(Simulation::AnalysisType::TRANSIENT) == "Transient");
    assert(Simulation::analysisToString(Simulation::AnalysisType::NOISE) == "Noise");
    
    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// RUN ALL TESTS
// ============================================================================

inline int run_all_simulation_tests() {
    std::cout << "\n=== SIMULATION SUITE TESTS ===\n\n";
    
    int passed = 0;
    int failed = 0;
    
    // Profile tests
    std::cout << "[Profile Tests]\n";
    if (test_profile_creation()) passed++; else failed++;
    if (test_profile_status_transitions()) passed++; else failed++;
    
    // Run tests
    std::cout << "\n[Simulation Run Tests]\n";
    if (test_run_creation()) passed++; else failed++;
    if (test_run_summary()) passed++; else failed++;
    
    // Corner tests
    std::cout << "\n[Corner Config Tests]\n";
    if (test_corner_config()) passed++; else failed++;
    
    // QuickAnalysis tests
    std::cout << "\n[Quick Analysis Tests]\n";
    if (test_quick_analysis_empty_netlist()) passed++; else failed++;
    if (test_quick_analysis_simple_circuit()) passed++; else failed++;
    if (test_quick_analysis_caching()) passed++; else failed++;
    if (test_quick_analysis_validation()) passed++; else failed++;
    
    // Utility tests
    std::cout << "\n[Utility Tests]\n";
    if (test_analysis_type_string()) passed++; else failed++;
    
    // Summary
    std::cout << "\n=== RESULTS ===\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";
    std::cout << "Total:  " << (passed + failed) << "\n\n";
    
    return failed;
}

} // namespace SimulationTests
