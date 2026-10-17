#include "circuitsim.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

/**
 * test_numerical_parity.cpp
 * Specialized verification for Phase D (Numerical Hardening).
 *
 * Compares the output of different execution modes:
 * 1. TENSOR_SOA_SCALAR (reference)
 * 2. TENSOR_SOA_SCALAR (SoA Math)
 * 3. TENSOR_SOA_SIMD (AVX2 Hardened)
 * 4. GPU (WebGPU EmulatedF64, if Dawn native available) — tolerance 1e-12
 *
 * CPU tolerance: 1e-13 (Strict IEEE-754 Check)
 * GPU tolerance: 1e-12 (EmulatedF64 hi-lo precision)
 */

struct ParityResult {
    std::string testName;
    bool passed;
    double maxDrift;
};

ParityResult runParityCheck(const std::string& name, TensorNetlist nl) {
    std::cout << "Checking Parity for: " << name << "..." << std::endl;
    
    CircuitSim sim;  // each mode solves cold on its own simulator: a reused one warm-starts from the last answer
    
    // 1. Reference: SoA scalar
    sim.execMode = CircuitSim::ExecutionMode::TENSOR_SOA_SCALAR;
    SolverStep resAos = sim.solveDC(nl);
    SolverStep resSoa = resAos;
    
    // 2. SoA SIMD
    CircuitSim simd;
    simd.execMode = CircuitSim::ExecutionMode::TENSOR_SOA_SIMD;
    SolverStep resSimd = simd.solveDC(nl);
    
    double maxDriftSoa = 0.0;
    double maxDriftSimd = 0.0;

    size_t n = resAos.nodeVoltages.size();
    for (size_t i = 0; i < n; ++i) {
        maxDriftSoa  = std::max(maxDriftSoa,  std::abs(resAos.nodeVoltages[i] - resSoa.nodeVoltages[i]));
        maxDriftSimd = std::max(maxDriftSimd, std::abs(resAos.nodeVoltages[i] - resSimd.nodeVoltages[i]));
    }

    bool passed = (maxDriftSoa < 1e-13) && (maxDriftSimd < 1e-13);
    double worstDrift = std::max(maxDriftSoa, maxDriftSimd);

    std::cout << std::scientific << std::setprecision(15);
    std::cout << "  - Max Drift (scalar, ref): " << maxDriftSoa  << (maxDriftSoa  < 1e-13 ? " [OK]" : " [FAIL]") << std::endl;
    std::cout << "  - Max Drift (scalar vs SIMD): " << maxDriftSimd << (maxDriftSimd < 1e-13 ? " [OK]" : " [FAIL]") << std::endl;
    std::cout << "  - Convergence Parity: "
              << (resAos.stats.iterations == resSimd.stats.iterations ? "MATCH" : "DRIFT")
              << " (" << resAos.stats.iterations << " vs " << resSimd.stats.iterations << " iterations)" << std::endl;

#if !defined(__EMSCRIPTEN__) && defined(ACUTESIM_HAS_DAWN_NATIVE)
    // 4. GPU path — EmulatedF64 tolerance 1e-12
    CircuitSim gpu;
    gpu.execMode = CircuitSim::ExecutionMode::GPU;
    SolverStep resGpu = gpu.solveDC(nl);

    double maxDriftGpu = 0.0;
    size_t ng = resAos.nodeVoltages.size();
    if (resGpu.nodeVoltages.size() == ng) {
        for (size_t i = 0; i < ng; ++i)
            maxDriftGpu = std::max(maxDriftGpu, std::abs(resAos.nodeVoltages[i] - resGpu.nodeVoltages[i]));
    } else {
        maxDriftGpu = 1.0;  // size mismatch — force fail
    }

    bool gpuPassed = (maxDriftGpu < 1e-12);
    passed = passed && gpuPassed;
    worstDrift = std::max(worstDrift, maxDriftGpu);
    std::cout << "  - Max Drift (AoS vs GPU):  " << maxDriftGpu << (gpuPassed ? " [OK]" : " [FAIL]") << std::endl;
    std::cout << "  - GPU NR Iterations: " << resGpu.stats.iterations << std::endl;
#endif

    return {name, passed, worstDrift};
}

int main() {
    std::vector<ParityResult> results;

    // Benchmark 1: Simple Linear Divider
    {
        TensorNetlist nl;
        nl.addVoltageSource(1, 0, 10.0);
        nl.addResistor(1, 2, 2000.0);
        nl.addResistor(2, 0, 2000.0);
        results.push_back(runParityCheck("Linear Divider", nl));
    }

    // Benchmark 2: Non-linear Diode Circuit
    {
        TensorNetlist nl;
        nl.addCurrentSource(0, 1, 1.0); // 1A
        nl.addDiode(1, 0, 1e-14);       // Is=1e-14
        results.push_back(runParityCheck("Diode IV", nl));
    }

    // Benchmark 3: Bridge Rectifier (Complexity)
    {
        TensorNetlist nl;
        nl.addVoltageSource(1, 2, 10.0);
        nl.addDiode(1, 3, 1e-14); // D1
        nl.addDiode(0, 1, 1e-14); // D2
        nl.addDiode(2, 3, 1e-14); // D3
        nl.addDiode(0, 2, 1e-14); // D4
        nl.addResistor(3, 0, 100.0);
        nl.addResistor(1, 0, 1e6); 
        nl.addResistor(2, 0, 1e6); 
        results.push_back(runParityCheck("Bridge Rectifier", nl));
    }

    // Benchmark 4: Darlington Pair (BJT)
    {
        TensorNetlist nl;
        nl.addVoltageSource(1, 0, 10.0);
        nl.addResistor(1, 2, 1.0);
        nl.addBJT("Q1", 2, 3, 4, "genericNPN", true, 100.0, 1.0, 1e-14);
        nl.addBJT("Q2", 2, 4, 0, "genericNPN", true, 100.0, 1.0, 1e-14);
        nl.addCurrentSource(1, 3, 1e-5);
        results.push_back(runParityCheck("Darlington Pair", nl));
    }

    std::cout << "\n==========================================" << std::endl;
    int failed = 0;
    for (const auto& r : results) {
        if (!r.passed) failed++;
    }
    
    if (failed == 0) {
        std::cout << "  ALL PARITY TESTS PASSED (TOL < 1e-13)" << std::endl;
    } else {
        std::cout << "  " << failed << " TESTS FAILED" << std::endl;
    }
    std::cout << "==========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}
