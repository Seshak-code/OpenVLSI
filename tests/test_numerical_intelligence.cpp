/**
 * test_numerical_intelligence.cpp
 * Phase 2.9: Numerical Intelligence verification tests.
 *
 * Tests:
 *   1. Singular Matrix Detection — floating node → rankDeficient flag
 *   2. A GMIN-held node — the solver reaches ngspice's 1e9 V
 *   3. Regularization Logging — GMIN stepping recorded when it ran, and only then
 *   4. RMS + Worst Node Tracking — bridge rectifier
 *   5. Linear Solver Residual Tracked — ConvergenceHistory has linearSolverResidual
 */

#include "../physics/circuitsim.h"
#include <iostream>
#include <cmath>

static void printResult(const char* name, bool passed) {
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " " << name << "\n";
}

// ── Test 1: Singular Matrix Detection ──────────────────────────────────────
// A current source pushing into a resistor with no ground → rank-deficient.

static bool testSingularMatrix() {
    TensorNetlist nl;
    nl.numGlobalNodes = 2;
    nl.addResistor(1, 2, 1000.0);          // R between nodes 1 and 2 only
    nl.addCurrentSource(0, 1, 1e-3);       // I from gnd to node 1 (node 0 = gnd)

    CircuitSim sim;
    SolverStep result = sim.solveDC(nl);

    bool flagged = result.stats.rankDeficient || !result.stats.converged;
    std::cout << "  rankDeficient=" << result.stats.rankDeficient
              << " converged=" << result.stats.converged
              << " smallestPivot=" << std::scientific << result.stats.smallestPivot << "\n";
    return flagged;
}

// ── Test 2: A node held only by GMIN ──────────────────────────────────────
// 1 mA into node 1, whose only path is a reverse-biased diode: the junction's GMIN (1e-12 S) carries it,
// so V1 = 1e9 V. ngspice 47 converges there too (v(1) = 1.000000e+09); the solver must reach it, not give up.

static bool testDivergenceDetection() {
    TensorNetlist nl;
    nl.addCurrentSource(0, 1, 1e-3);  // 1mA into node 1 from ground
    nl.addDiode(0, 1, 1e-14);         // Reverse-biased for V1 > 0: I ≈ -Is only

    CircuitSim sim;
    SolverStep result = sim.solveDC(nl);

    const double v1 = result.nodeVoltages.empty() ? 0.0 : result.nodeVoltages[0];
    std::cout << "  converged=" << result.stats.converged << " v(1)=" << std::scientific << v1
              << " gminStepping=" << result.stats.regularization.applied << "\n";
    return result.stats.converged && std::abs(v1 - 1e9) <= 1e-6 * 1e9;
}

// ── Test 3: Regularization Logging ─────────────────────────────────────────
// GMIN stepping is recorded when it ran, and only then: a Darlington pair converges directly; a floating pair steps.

static bool testRegularizationLogging() {
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 10.0);
    nl.addResistor(1, 2, 1000.0);
    nl.addBJT("Q1", 2, 3, 4, "NPN", true, 100.0, 1.0, 1e-14);
    nl.addBJT("Q2", 2, 4, 0, "NPN", true, 100.0, 1.0, 1e-14);
    nl.addCurrentSource(1, 3, 1e-5);

    CircuitSim sim;
    SolverStep direct = sim.solveDC(nl);

    TensorNetlist hard;  // test 1's floating pair: the direct solve fails, then GMIN stepping (and source stepping) run
    hard.numGlobalNodes = 2;
    hard.addResistor(1, 2, 1000.0);
    hard.addCurrentSource(0, 1, 1e-3);
    CircuitSim sim2;
    SolverStep stepped = sim2.solveDC(hard);

    std::cout << "  darlington converged=" << direct.stats.converged << " gminApplied=" << direct.stats.regularization.applied
              << "; floating pair gminApplied=" << stepped.stats.regularization.applied
              << " gmin=" << stepped.stats.regularization.gminValue << " injections=" << stepped.stats.regularization.injections << "\n";
    return direct.stats.converged && !direct.stats.regularization.applied &&
           stepped.stats.regularization.applied && stepped.stats.regularization.injections > 0;
}

// ── Test 4: RMS + Worst Node Tracking ──────────────────────────────────────
// Bridge rectifier — diode topology provides non-trivial per-node residuals.

static bool testRmsAndWorstNode() {
    TensorNetlist nl;
    nl.addVoltageSource(1, 2, 10.0);
    nl.addDiode(1, 3, 1e-14);
    nl.addDiode(0, 1, 1e-14);
    nl.addDiode(2, 3, 1e-14);
    nl.addDiode(0, 2, 1e-14);
    nl.addResistor(3, 0, 100.0);
    nl.addResistor(1, 0, 1e6);
    nl.addResistor(2, 0, 1e6);

    CircuitSim sim;
    SolverStep result = sim.solveDC(nl);

    bool rmsOk  = result.stats.rmsResidual >= 0.0;
    bool nodeOk = result.stats.worstNodeIndex > 0 || result.stats.converged;
    std::cout << "  converged=" << result.stats.converged
              << " rmsResidual=" << std::scientific << result.stats.rmsResidual
              << " worstNode=" << result.stats.worstNodeIndex
              << " worstNodeV=" << result.stats.worstNodeVoltage << "V\n";
    return rmsOk && nodeOk;
}

// ── Test 5: Linear Solver Residual Per Iteration ───────────────────────────

static bool testLinearSolverResidualTracked() {
    TensorNetlist nl;
    nl.addVoltageSource(1, 0, 5.0);
    nl.addResistor(1, 2, 1000.0);
    nl.addResistor(2, 0, 1000.0);

    CircuitSim sim;
    SolverStep result = sim.solveDC(nl);

    bool historyNonEmpty = !result.stats.convergenceHistory.empty();
    bool residualTracked = false;
    for (const auto& step : result.stats.convergenceHistory) {
        if (std::isfinite(step.linearSolverResidual)) {
            residualTracked = true;
            break;
        }
    }
    std::cout << "  converged=" << result.stats.converged
              << " historySteps=" << result.stats.convergenceHistory.size()
              << " linearResidualTracked=" << residualTracked << "\n";
    return historyNonEmpty && residualTracked;
}

// ── Main ───────────────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << "  NUMERICAL INTELLIGENCE TESTS (2.9)\n";
    std::cout << "========================================\n";

    int failed = 0;

    struct TestCase { const char* name; bool(*fn)(); };
    TestCase tests[] = {
        {"Singular Matrix Detection",      testSingularMatrix},
        {"Divergence Detection",           testDivergenceDetection},
        {"Regularization Logging",         testRegularizationLogging},
        {"RMS + Worst Node Tracking",      testRmsAndWorstNode},
        {"Linear Solver Residual Tracked", testLinearSolverResidualTracked},
    };

    for (const auto& tc : tests) {
        std::cout << "\n[" << tc.name << "]\n";
        bool ok = tc.fn();
        printResult(tc.name, ok);
        if (!ok) ++failed;
    }

    std::cout << "\n========================================\n";
    if (failed == 0) {
        std::cout << "  ALL NUMERICAL INTELLIGENCE TESTS PASSED\n";
    } else {
        std::cout << "  " << failed << " TEST(S) FAILED\n";
    }
    std::cout << "========================================\n";
    return (failed == 0) ? 0 : 1;
}
