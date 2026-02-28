/**
 * test_sparse_ac.cpp
 * Phase 2.10: Sparse AC/Noise parity tests.
 *
 * Tests:
 *   1. Sparse vs Dense AC Parity    — RC low-pass filter, 100 freq points, < 1e-10 deviation
 *   2. Noise Parity                 — Common-emitter amplifier, integrated noise within 0.1%
 *   3. AC with Pattern Cache         — assembleFast produces same result as createCsr
 *   4. Performance Sanity           — Sparse path terminates without crashing
 */

#include "acutesim_engine/solvers/ac_solver.h"
#include "../physics/circuitsim.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <algorithm>

static void printResult(const char* name, bool passed) {
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " " << name << "\n";
}

// ── Test 1: Sparse vs Dense AC Parity ──────────────────────────────────────

static bool testAcSparseDenseParity() {
    // RC low-pass: Vs=1V → R=1k → node1 (2) → C=1nF → GND
    // node 1 = voltage source positive, node 2 = RC junction
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 1.0);
    nl.addResistor(1, 2, 1000.0);
    nl.addCapacitor(2, 0, 1e-9);

    std::vector<double> dcSolution = {1.0, 1.0};

    // Dense path (original)
    ACSolver::ACResult dense  = ACSolver::solveAC(nl, dcSolution, 1e3, 100e6, 100, 1, 1.0);
    // Sparse path
    ACSolver::ACResult sparse = ACSolver::solveAC_Sparse(nl, dcSolution, 1e3, 100e6, 100, 1, 1.0);

    if (dense.nodeVoltages.size() != sparse.nodeVoltages.size()) {
        std::cout << "  Size mismatch\n";
        return false;
    }

    double maxDev = 0.0;
    for (size_t fi = 0; fi < dense.nodeVoltages.size(); ++fi) {
        size_t n = std::min(dense.nodeVoltages[fi].size(), sparse.nodeVoltages[fi].size());
        for (size_t ni = 0; ni < n; ++ni) {
            double dev = std::abs(dense.nodeVoltages[fi][ni] - sparse.nodeVoltages[fi][ni]);
            maxDev = std::max(maxDev, dev);
        }
    }

    std::cout << "  Max complex voltage deviation: " << std::scientific << maxDev << "\n";
    return maxDev < 1e-10;
}

// ── Test 2: Noise Parity ────────────────────────────────────────────────────

static bool testNoiseParity() {
    // NPN CE amp: Vcc → Rc → collector(2) → BJT(Q1) → emitter(0), base(3)
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 5.0);
    nl.addResistor(1, 2, 1000.0);
    nl.addBJT("Q1", 2, 3, 0, "NPN", true, 100.0, 1.0, 1e-14);
    nl.addResistor(1, 3, 100000.0);

    std::vector<double> dcSolution = {5.0, 4.0, 0.7, 0.0};

    ACSolver::NoiseResult dense  = ACSolver::solveNoise(nl, dcSolution, 2, 0, 1e3, 100e6, 30);
    ACSolver::NoiseResult sparse = ACSolver::solveNoise_Sparse(nl, dcSolution, 2, 0, 1e3, 100e6, 30);

    if (dense.output_noise_psd.size() != sparse.output_noise_psd.size()) {
        std::cout << "  Size mismatch\n";
        return false;
    }

    double relDiff = 0.0;
    if (dense.integrated_noise_vrms > 1e-20) {
        relDiff = std::abs(dense.integrated_noise_vrms - sparse.integrated_noise_vrms)
                  / dense.integrated_noise_vrms;
    }

    std::cout << "  Dense  vrms=" << std::scientific << dense.integrated_noise_vrms << "\n";
    std::cout << "  Sparse vrms=" << std::scientific << sparse.integrated_noise_vrms << "\n";
    std::cout << "  Relative difference: " << relDiff * 100.0 << "%\n";
    return relDiff < 0.001;
}

// ── Test 3: AC with Pattern Cache ──────────────────────────────────────────

static bool testAcWithPatternCache() {
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 1.0);
    nl.addResistor(1, 2, 1000.0);
    nl.addCapacitor(2, 0, 1e-9);

    std::vector<double> dcSolution = {1.0, 1.0};

    // Run DC to warm up cached pattern
    CircuitSim sim;
    TensorNetlist nl2 = nl;
    sim.solveDC(nl2);
    const CachedCsrPattern* pattern = sim.getCachedPattern();

    ACSolver::ACResult withCache    = ACSolver::solveAC_Sparse(nl, dcSolution, 1e3, 100e6, 50, 1, 1.0, pattern);
    ACSolver::ACResult withoutCache = ACSolver::solveAC_Sparse(nl, dcSolution, 1e3, 100e6, 50, 1, 1.0, nullptr);

    double maxDev = 0.0;
    for (size_t fi = 0; fi < withCache.nodeVoltages.size(); ++fi) {
        size_t n = std::min(withCache.nodeVoltages[fi].size(), withoutCache.nodeVoltages[fi].size());
        for (size_t ni = 0; ni < n; ++ni) {
            double dev = std::abs(withCache.nodeVoltages[fi][ni] - withoutCache.nodeVoltages[fi][ni]);
            maxDev = std::max(maxDev, dev);
        }
    }

    std::cout << "  Pattern available: " << (pattern ? "yes" : "no") << "\n";
    std::cout << "  Max deviation (cached vs uncached): " << std::scientific << maxDev << "\n";
    // If pattern is available, should match; if null (no pattern), both use createCsr → identical
    return maxDev < 1e-12;
}

// ── Test 4: Performance Sanity ─────────────────────────────────────────────

static bool testPerformanceSanity() {
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 5.0);
    nl.addResistor(1, 2, 1000.0);
    nl.addResistor(2, 3, 470.0);
    nl.addCapacitor(2, 0, 100e-12);
    nl.addDiode(3, 0, 1e-14);

    std::vector<double> dcSolution = {5.0, 3.0, 0.6};
    int numPoints = 200;

    auto t0 = std::chrono::steady_clock::now();
    ACSolver::solveAC(nl, dcSolution, 1e3, 1e9, numPoints);
    auto t1 = std::chrono::steady_clock::now();
    double denseMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    auto t2 = std::chrono::steady_clock::now();
    ACSolver::solveAC_Sparse(nl, dcSolution, 1e3, 1e9, numPoints);
    auto t3 = std::chrono::steady_clock::now();
    double sparseMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::cout << "  Dense:  " << denseMs  << " ms\n";
    std::cout << "  Sparse: " << sparseMs << " ms\n";
    std::cout << "  Ratio (sparse/dense): " << sparseMs / denseMs << "\n";

    // Both must complete successfully and sparse must not be wildly slower (< 10x for small circuits)
    return (sparseMs / denseMs) < 10.0;
}

// ── Main ───────────────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << "  SPARSE AC REFACTOR TESTS (2.10)\n";
    std::cout << "========================================\n";

    int failed = 0;

    struct TestCase { const char* name; bool(*fn)(); };
    TestCase tests[] = {
        {"Sparse vs Dense AC Parity", testAcSparseDenseParity},
        {"Noise Parity",              testNoiseParity},
        {"AC with Pattern Cache",     testAcWithPatternCache},
        {"Performance Sanity",        testPerformanceSanity},
    };

    for (const auto& tc : tests) {
        std::cout << "\n[" << tc.name << "]\n";
        bool ok = tc.fn();
        printResult(tc.name, ok);
        if (!ok) ++failed;
    }

    std::cout << "\n========================================\n";
    if (failed == 0) {
        std::cout << "  ALL SPARSE AC TESTS PASSED\n";
    } else {
        std::cout << "  " << failed << " TEST(S) FAILED\n";
    }
    std::cout << "========================================\n";
    return (failed == 0) ? 0 : 1;
}
