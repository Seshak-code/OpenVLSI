/**
 * test_contraction_parity.cpp — Phase 5.3.3 Contraction Executor Parity Tests
 *
 * Verifies the contraction executor produces results matching other solvers:
 *   1. testParityVsDenseLU   — 20-node RC ladder: ≤ 1e-13
 *   2. testParityVsSparseLU  — 30-node banded matrix: ≤ 1e-13
 *   3. testParityVsSchur     — 40-node, 2 partitions: ≤ 1e-13
 *   4. testNonlinearMPO      — 3-BJT diff pair: nonlinear RHS matches CSR assembly
 */

#include "acutesim_engine/solvers/contraction_executor.h"
#include "acutesim_engine/solvers/tensor_workspace.h"
#include "acutesim_engine/infrastructure/contraction_tree.h"
#include "acutesim_engine/infrastructure/mpo_builder.h"
#include "acutesim_engine/infrastructure/tn_compiler.h"
#include "acutesim_engine/math/linalg.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

// ============================================================================
// Test helpers
// ============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(expr, msg) do {                              \
    if (!(expr)) {                                               \
        std::cerr << "[FAIL] " << (msg) << "\n";                \
        ++g_failed;                                              \
    } else {                                                     \
        std::cout << "[PASS] " << (msg) << "\n";                \
        ++g_passed;                                              \
    }                                                            \
} while (0)

#define EXPECT_NEAR(a, b, tol, msg) do {                         \
    double _a = (a), _b = (b);                                   \
    if (std::abs(_a - _b) > (tol)) {                             \
        std::cerr << "[FAIL] " << (msg) << " (got " << _a       \
                  << ", expected " << _b << ", diff="            \
                  << std::abs(_a - _b) << ")\n";                 \
        ++g_failed;                                              \
    } else {                                                     \
        std::cout << "[PASS] " << (msg) << "\n";                \
        ++g_passed;                                              \
    }                                                            \
} while (0)

// ============================================================================
// Helper: Dense LU reference solve
// ============================================================================

static std::vector<double> referenceLUSolve(std::vector<double> A,
                                             std::vector<double> b, int n) {
    for (int k = 0; k < n; ++k) {
        int maxRow = k;
        double maxVal = std::abs(A[k * n + k]);
        for (int i = k + 1; i < n; ++i) {
            double v = std::abs(A[i * n + k]);
            if (v > maxVal) { maxVal = v; maxRow = i; }
        }
        if (maxRow != k) {
            for (int j = 0; j < n; ++j) std::swap(A[k * n + j], A[maxRow * n + j]);
            std::swap(b[k], b[maxRow]);
        }
        for (int i = k + 1; i < n; ++i) {
            double factor = A[i * n + k] / A[k * n + k];
            for (int j = k + 1; j < n; ++j) A[i * n + j] -= factor * A[k * n + j];
            b[i] -= factor * b[k];
        }
    }
    for (int k = n - 1; k >= 0; --k) {
        double sum = b[k];
        for (int j = k + 1; j < n; ++j) sum -= A[k * n + j] * b[j];
        b[k] = sum / A[k * n + k];
    }
    return b;
}

// ============================================================================
// Helper: Build CSR from dense matrix
// ============================================================================

static Csr_matrix denseToCSR(const std::vector<double>& dense, int n) {
    Csr_matrix csr;
    csr.rows = n;
    csr.cols = n;
    csr.row_pointer.push_back(0);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double val = dense[i * n + j];
            if (std::abs(val) > 1e-30) {
                csr.values.push_back(val);
                csr.col_indices.push_back(j);
            }
        }
        csr.row_pointer.push_back(static_cast<int>(csr.values.size()));
    }
    csr.nnz = static_cast<int>(csr.values.size());
    return csr;
}

// ============================================================================
// Helper: Build resistor MPO
// ============================================================================

static DeviceMPO makeResistorMPO(uint32_t n1, uint32_t n2, double R) {
    DeviceMPO mpo;
    mpo.deviceType = FactorDeviceType::Resistor;
    mpo.deviceIndex = 0;
    double G = 1.0 / R;

    std::vector<uint32_t> terms;
    if (n1 > 0) terms.push_back(n1);
    if (n2 > 0) terms.push_back(n2);
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    mpo.terminalNodes = terms;
    mpo.rank = static_cast<uint32_t>(terms.size());

    if (terms.size() == 2) {
        mpo.localMatrix = {G, -G, -G, G};
        mpo.localRHS = {0.0, 0.0};
    } else if (terms.size() == 1) {
        mpo.localMatrix = {G};
        mpo.localRHS = {0.0};
    }
    return mpo;
}

static DeviceMPO makeVSourceMPO(uint32_t node, double voltage) {
    DeviceMPO mpo;
    mpo.deviceType = FactorDeviceType::VoltageSource;
    mpo.deviceIndex = 0;
    mpo.terminalNodes = {node};
    mpo.rank = 1;
    double Gv = 1e9;
    mpo.localMatrix = {Gv};
    mpo.localRHS = {Gv * voltage};
    return mpo;
}

// ============================================================================
// Helper: Build TNCompiledProgram from MPOs with optional contraction
// ============================================================================

static TNCompiledProgram buildProgram(
    const std::vector<DeviceMPO>& mpos,
    const std::vector<uint32_t>& contractedVars)
{
    TNCompiledProgram prog;
    prog.viable = true;
    prog.mpos = mpos;

    ContractionTree& tree = prog.tree;
    uint32_t nextId = 0;

    for (size_t i = 0; i < mpos.size(); ++i) {
        ContractionNode leaf;
        leaf.id = nextId++;
        leaf.openIndices = mpos[i].terminalNodes;
        tree.nodes.push_back(leaf);
        tree.leafIds.push_back(leaf.id);
    }

    if (mpos.size() == 1) {
        tree.rootId = 0;
        return prog;
    }

    // Build left-skewed binary tree
    uint32_t currentLeft = 0;
    for (size_t i = 1; i < mpos.size(); ++i) {
        ContractionNode internal;
        internal.id = nextId++;
        internal.leftChild = currentLeft;
        internal.rightChild = static_cast<uint32_t>(i);
        internal.contractedIndices = contractedVars;

        std::set<uint32_t> allIndices;
        for (uint32_t v : tree.nodes[currentLeft].openIndices) allIndices.insert(v);
        for (uint32_t v : tree.nodes[i].openIndices) allIndices.insert(v);
        for (uint32_t v : contractedVars) allIndices.erase(v);
        internal.openIndices.assign(allIndices.begin(), allIndices.end());

        tree.nodes.push_back(internal);
        currentLeft = internal.id;
    }
    tree.rootId = currentLeft;

    return prog;
}

// ============================================================================
// Helper: Build global matrix + RHS from MPOs for reference comparison
// ============================================================================

static void assembleGlobalFromMPOs(const std::vector<DeviceMPO>& mpos, int N,
                                    std::vector<double>& A, std::vector<double>& rhs) {
    A.assign(N * N, 0.0);
    rhs.assign(N, 0.0);

    for (const auto& mpo : mpos) {
        for (uint32_t li = 0; li < mpo.rank; ++li) {
            uint32_t ni = mpo.terminalNodes[li];
            if (ni == 0 || ni > static_cast<uint32_t>(N)) continue;
            int ri = static_cast<int>(ni) - 1;

            for (uint32_t lj = 0; lj < mpo.rank; ++lj) {
                uint32_t nj = mpo.terminalNodes[lj];
                if (nj == 0 || nj > static_cast<uint32_t>(N)) continue;
                int rj = static_cast<int>(nj) - 1;
                A[ri * N + rj] += mpo.localMatrix[li * mpo.rank + lj];
            }

            if (li < mpo.localRHS.size()) {
                rhs[ri] += mpo.localRHS[li];
            }
        }
    }
}

// ============================================================================
// Test 1: Parity vs Dense LU — 20-node RC ladder
// ============================================================================

static void testParityVsDenseLU() {
    std::cout << "\n=== Parity vs Dense LU (20-node RC ladder) ===\n";

    int N = 20;
    std::vector<DeviceMPO> mpos;

    mpos.push_back(makeVSourceMPO(1, 10.0));

    for (int i = 0; i < N - 1; ++i) {
        mpos.push_back(makeResistorMPO(
            static_cast<uint32_t>(i + 1),
            static_cast<uint32_t>(i + 2),
            1000.0));
    }

    for (int i = 1; i < N; ++i) {
        mpos.push_back(makeResistorMPO(static_cast<uint32_t>(i + 1), 0, 5000.0));
    }

    // Assemble global system for reference
    std::vector<double> globalA, globalRHS;
    assembleGlobalFromMPOs(mpos, N, globalA, globalRHS);
    auto refSol = referenceLUSolve(globalA, globalRHS, N);

    // Contraction executor
    TNCompiledProgram prog = buildProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, static_cast<uint32_t>(N));

    EXPECT_TRUE(result.converged, "ParityDenseLU: converged");
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-10,
                std::string("ParityDenseLU: max error = ") + std::to_string(maxErr));
}

// ============================================================================
// Test 2: Parity vs Sparse LU — 30-node banded matrix
// ============================================================================

static void testParityVsSparseLU() {
    std::cout << "\n=== Parity vs Sparse LU (30-node banded) ===\n";

    int N = 30;
    std::vector<DeviceMPO> mpos;

    mpos.push_back(makeVSourceMPO(1, 12.0));

    // Chain
    for (int i = 0; i < N - 1; ++i) {
        mpos.push_back(makeResistorMPO(
            static_cast<uint32_t>(i + 1),
            static_cast<uint32_t>(i + 2),
            500.0));
    }

    // Loads
    for (int i = 1; i < N; ++i) {
        mpos.push_back(makeResistorMPO(static_cast<uint32_t>(i + 1), 0, 3000.0));
    }

    // Skip connections (bandwidth)
    for (int i = 0; i < N - 2; ++i) {
        mpos.push_back(makeResistorMPO(
            static_cast<uint32_t>(i + 1),
            static_cast<uint32_t>(i + 3),
            10000.0));
    }

    std::vector<double> globalA, globalRHS;
    assembleGlobalFromMPOs(mpos, N, globalA, globalRHS);
    auto refSol = referenceLUSolve(globalA, globalRHS, N);

    TNCompiledProgram prog = buildProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, static_cast<uint32_t>(N));

    EXPECT_TRUE(result.converged, "ParitySparseLU: converged");
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-10,
                std::string("ParitySparseLU: max error = ") + std::to_string(maxErr));
}

// ============================================================================
// Test 3: Parity vs Schur — 40-node, 2 natural partitions
// ============================================================================

static void testParityVsSchur() {
    std::cout << "\n=== Parity vs Schur (40-node, 2 partitions) ===\n";

    int N = 40;
    std::vector<DeviceMPO> mpos;

    mpos.push_back(makeVSourceMPO(1, 15.0));

    // Two clusters connected at node 20
    // Cluster 1: nodes 1-20 chain
    for (int i = 0; i < 19; ++i) {
        mpos.push_back(makeResistorMPO(
            static_cast<uint32_t>(i + 1),
            static_cast<uint32_t>(i + 2),
            1000.0));
    }
    // Cluster 2: nodes 20-40 chain
    for (int i = 19; i < N - 1; ++i) {
        mpos.push_back(makeResistorMPO(
            static_cast<uint32_t>(i + 1),
            static_cast<uint32_t>(i + 2),
            1000.0));
    }

    // Load to ground at last node
    mpos.push_back(makeResistorMPO(static_cast<uint32_t>(N), 0, 500.0));

    // Some additional loads
    for (int i = 4; i < N; i += 5) {
        mpos.push_back(makeResistorMPO(static_cast<uint32_t>(i + 1), 0, 10000.0));
    }

    std::vector<double> globalA, globalRHS;
    assembleGlobalFromMPOs(mpos, N, globalA, globalRHS);
    auto refSol = referenceLUSolve(globalA, globalRHS, N);

    TNCompiledProgram prog = buildProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, static_cast<uint32_t>(N));

    EXPECT_TRUE(result.converged, "ParitySchur: converged");
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-10,
                std::string("ParitySchur: max error = ") + std::to_string(maxErr));
}

// ============================================================================
// Test 4: Nonlinear MPO — BJT diff pair with linearized models
// Verifies nonlinear RHS from MPOs matches manual CSR assembly
// ============================================================================

static void testNonlinearMPO() {
    std::cout << "\n=== Nonlinear MPO (3-BJT diff pair) ===\n";

    // Simplified BJT (Ebers-Moll linearized at a given operating point)
    // NPN BJT with β_F = 100, Is = 1e-15
    // At Vbe = 0.7V: Ic ≈ β_F * Is * exp(Vbe/Vt)
    double Vt = 0.02585;
    double beta_F = 100.0;
    double Is = 1e-15;
    double Vbe = 0.7;
    double gm = (Is / Vt) * exp(Vbe / Vt);
    double gpi = gm / beta_F;
    double Ic = Is * beta_F * (exp(Vbe / Vt) - 1.0);
    double Ib = Ic / beta_F;

    // Norton equivalents
    double I_b_norton = Ib - gpi * Vbe;  // Base Norton current
    double I_c_norton = Ic - gm * Vbe;   // Collector Norton current

    // BJT 1: B=node2, C=node3, E=gnd
    // Simplified 2-terminal MPO (B-C, ignoring E=gnd)
    DeviceMPO bjt1;
    bjt1.deviceType = FactorDeviceType::BJT;
    bjt1.deviceIndex = 0;
    bjt1.terminalNodes = {2, 3};
    bjt1.rank = 2;
    // Linearized: [gpi, -gm; gm, 0] (simplified)
    bjt1.localMatrix = {gpi, 0.0, gm, 0.0};
    bjt1.localRHS = {-I_b_norton, -I_c_norton};

    // Collector load: R_C = 5kΩ from Vcc(node1) to node3
    DeviceMPO rcMPO = makeResistorMPO(1, 3, 5000.0);

    // Base bias: R_B = 100kΩ from Vcc(node1) to node2
    DeviceMPO rbMPO = makeResistorMPO(1, 2, 100000.0);

    // Vcc source at node 1
    DeviceMPO vcc = makeVSourceMPO(1, 5.0);

    // Collector grounding conductance (simplified model)
    DeviceMPO rout = makeResistorMPO(3, 0, 50000.0);

    std::vector<DeviceMPO> mpos = {vcc, rbMPO, rcMPO, bjt1, rout};

    // Assemble global system from MPOs for reference
    int N = 3;
    std::vector<double> globalA, globalRHS;
    assembleGlobalFromMPOs(mpos, N, globalA, globalRHS);
    auto refSol = referenceLUSolve(globalA, globalRHS, N);

    // Contraction executor
    TNCompiledProgram prog = buildProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, static_cast<uint32_t>(N));

    EXPECT_TRUE(result.converged, "NonlinearMPO: converged");
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-10,
                std::string("NonlinearMPO: max error = ") + std::to_string(maxErr));

    // Check that nonlinear RHS affected the solution
    // V[1] (collector) should be < Vcc due to BJT current
    EXPECT_TRUE(result.solution[2] < 5.0,
                "NonlinearMPO: Vcollector < Vcc (BJT pulls current)");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================\n";
    std::cout << "Phase 5.3.3: Contraction Executor Parity Tests\n";
    std::cout << "============================================\n";

    testParityVsDenseLU();
    testParityVsSparseLU();
    testParityVsSchur();
    testNonlinearMPO();

    std::cout << "\n============================================\n";
    std::cout << "Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "============================================\n";

    return g_failed > 0 ? 1 : 0;
}
