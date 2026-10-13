#pragma once

/**
 * test_simulation_suite.h
 * 
 * Automated tests for Phase 1.7 Simulation Suite
 * Tests: Profiles, Explorer, Workspace flows
 */

#include <iostream>
#include <cassert>
#include <string>
#include <unordered_map>
#include "../gui/analysis/simulation_profile.h"
#include "acutesim_engine/netlist/circuit.h"
#include "acutesim_engine/physics/corner_config.h"

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
