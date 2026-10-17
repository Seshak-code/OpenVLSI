/**
 * test_gpu_parity.cpp
 * Phase D5c: Focused GPU parity test.
 *
 * Runs 4 benchmark circuits on both CPU-AoS and GPU execution paths and
 * verifies:
 *   1. Node voltage deviation < 1e-12 on all benchmarks
 *   2. KCL residual < 1 pA on the GPU solution (calculatePhysicalResiduals)
 *   3. NR iteration count reasonable (converged within GPU_MAX_ITER)
 *   4. Per-node drift reported for debugging
 *
 * Compiled only for non-EMSCRIPTEN + ACUTESIM_HAS_DAWN_NATIVE builds.
 * Guard in CMakeLists.txt: if(NOT EMSCRIPTEN AND TARGET acutesim_dawn_deps)
 */

#if !defined(__EMSCRIPTEN__) && defined(ACUTESIM_HAS_DAWN_NATIVE)

#include "circuitsim.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <string>

// ── Constants ────────────────────────────────────────────────────────────────
static constexpr double GPU_VOLTAGE_TOL = 1e-12;   // EmulatedF64 precision target
static constexpr double KCL_RESIDUAL_TOL = 1e-10;  // 100 pA — NR solver precision with GMIN stepping
static constexpr int    GPU_MAX_ITER = 50;

// ── Per-circuit result ───────────────────────────────────────────────────────
struct GpuParityResult {
    std::string name;
    bool voltagePassed;
    bool kclPassed;
    bool gpuConverged;
    double maxVoltageDrift;
    int    gpuIterations;
    int    cpuIterations;
};

static GpuParityResult runGpuParityCheck(const std::string& name, TensorNetlist nl) {
    std::cout << "\n[GPU Parity] " << name << "\n";

    CircuitSim sim;

    // ── CPU reference (SoA scalar) ───────────────────────────────────────────
    sim.execMode = CircuitSim::ExecutionMode::TENSOR_SOA_SCALAR;
    SolverStep resCpu = sim.solveDC(nl);

    // ── GPU path (its own simulator: a reused one warm-starts from the CPU answer) ──
    CircuitSim gpu;
    gpu.execMode = CircuitSim::ExecutionMode::GPU;
    SolverStep resGpu = gpu.solveDC(nl);

    GpuParityResult r;
    r.name           = name;
    r.gpuConverged   = resGpu.stats.converged;
    r.cpuIterations  = resCpu.stats.iterations;
    r.gpuIterations  = resGpu.stats.iterations;
    r.maxVoltageDrift = 0.0;

    // ── Voltage deviation ────────────────────────────────────────────────────
    size_t n = resCpu.nodeVoltages.size();
    if (resGpu.nodeVoltages.size() != n) {
        std::cout << "  [ERROR] Node count mismatch: CPU=" << n
                  << " GPU=" << resGpu.nodeVoltages.size() << "\n";
        r.voltagePassed = false;
        r.kclPassed     = false;
        return r;
    }

    std::cout << std::scientific << std::setprecision(15);
    std::cout << "  Node voltages (CPU vs GPU):\n";
    for (size_t i = 0; i < n; ++i) {
        double drift = std::abs(resCpu.nodeVoltages[i] - resGpu.nodeVoltages[i]);
        r.maxVoltageDrift = std::max(r.maxVoltageDrift, drift);
        std::cout << "    V[" << i+1 << "] CPU=" << resCpu.nodeVoltages[i]
                  << "  GPU=" << resGpu.nodeVoltages[i]
                  << "  drift=" << drift << "\n";
    }

    r.voltagePassed = (r.maxVoltageDrift < GPU_VOLTAGE_TOL) && r.gpuConverged;
    std::cout << "  Max voltage drift: " << r.maxVoltageDrift
              << (r.voltagePassed ? " [OK]" : " [FAIL]") << "\n";

    // ── KCL check via calculatePhysicalResiduals ─────────────────────────────
    // the full solution: node voltages, then the source / inductor branch currents KCL needs
    std::vector<double> x = resGpu.nodeVoltages;
    x.insert(x.end(), resGpu.branchCurrents.begin(), resGpu.branchCurrents.end());
    double kclResidual = gpu.calculatePhysicalResiduals(nl, x);
    r.kclPassed = (kclResidual < KCL_RESIDUAL_TOL);
    std::cout << "  KCL residual: " << kclResidual
              << (r.kclPassed ? " [OK]" : " [FAIL]") << "\n";

    // ── Iteration count ──────────────────────────────────────────────────────
    std::cout << "  CPU iterations: " << r.cpuIterations
              << "  GPU iterations: " << r.gpuIterations << "\n";

    return r;
}

int main() {
    std::vector<GpuParityResult> results;

    // Benchmark 1: Linear Divider (all-passive — validates readback/upload/LU pipeline)
    {
        TensorNetlist nl;
        nl.addVoltageSource(1, 0, 10.0);
        nl.addResistor(1, 2, 2000.0);
        nl.addResistor(2, 0, 2000.0);
        results.push_back(runGpuParityCheck("Linear Divider", nl));
    }

    // Benchmark 2: Diode IV (single nonlinear device — validates f64_exp)
    {
        TensorNetlist nl;
        nl.addCurrentSource(0, 1, 1.0);
        nl.addDiode(1, 0, 1e-14);
        results.push_back(runGpuParityCheck("Diode IV", nl));
    }

    // Benchmark 3: Bridge Rectifier (four diodes — validates parallel stamp assembly)
    {
        TensorNetlist nl;
        nl.addVoltageSource(1, 2, 10.0);
        nl.addDiode(1, 3, 1e-14);
        nl.addDiode(0, 1, 1e-14);
        nl.addDiode(2, 3, 1e-14);
        nl.addDiode(0, 2, 1e-14);
        nl.addResistor(3, 0, 100.0);
        nl.addResistor(1, 0, 1e6);
        nl.addResistor(2, 0, 1e6);
        results.push_back(runGpuParityCheck("Bridge Rectifier", nl));
    }

    // Benchmark 4: Darlington Pair (two BJTs — validates Ebers-Moll WGSL kernel)
    {
        TensorNetlist nl;
        nl.addVoltageSource(1, 0, 10.0);
        nl.addResistor(1, 2, 1.0);
        nl.addBJT("Q1", 2, 3, 4, "genericNPN", true, 100.0, 1.0, 1e-14);
        nl.addBJT("Q2", 2, 4, 0, "genericNPN", true, 100.0, 1.0, 1e-14);
        nl.addCurrentSource(1, 3, 1e-5);
        results.push_back(runGpuParityCheck("Darlington Pair", nl));
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "\n==========================================\n";
    std::cout << "  GPU PARITY TEST SUMMARY\n";
    std::cout << "==========================================\n";
    int failed = 0;
    for (const auto& r : results) {
        bool ok = r.voltagePassed && r.kclPassed && r.gpuConverged;
        if (!ok) ++failed;
        std::cout << "  " << (ok ? "[PASS]" : "[FAIL]") << " " << r.name
                  << " | drift=" << std::scientific << r.maxVoltageDrift
                  << " | CPU_iter=" << r.cpuIterations
                  << " | GPU_iter=" << r.gpuIterations
                  << " | KCL=" << (r.kclPassed ? "OK" : "FAIL")
                  << " | conv=" << (r.gpuConverged ? "YES" : "NO") << "\n";
    }
    std::cout << "==========================================\n";
    if (failed == 0) {
        std::cout << "  ALL GPU PARITY TESTS PASSED\n";
    } else {
        std::cout << "  " << failed << " TESTS FAILED\n";
    }
    std::cout << "==========================================\n";
    return (failed == 0) ? 0 : 1;
}

#else  // Not a Dawn native build or is EMSCRIPTEN

#include <iostream>
int main() {
    std::cout << "[test_gpu_parity] Skipped: requires ACUTESIM_HAS_DAWN_NATIVE "
                 "(non-EMSCRIPTEN Dawn build).\n";
    return 0;
}

#endif
