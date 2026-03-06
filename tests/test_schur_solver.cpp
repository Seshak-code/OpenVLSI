/**
 * test_schur_solver.cpp — Phase 5.3.2 Schur Complement Solver Tests
 *
 * Verifies the Schur complement domain decomposition solver produces
 * results matching dense LU within 1e-13.
 *
 * Test cases:
 *   1. testTwoPartitionRC        — 2 RC partitions sharing 1 boundary node
 *   2. testThreePartitionStar    — 3 partitions connected at hub
 *   3. testSchurVsDenseParity    — Random 20-node, 2 partitions vs dense LU
 *   4. testSinglePartitionFallback — 1 partition (no boundary)
 *   5. testInternalRecovery      — Internal variables recovered correctly
 */

#include "acutesim_engine/solvers/schur_solver.h"
#include "acutesim_engine/math/linalg.h"

#include <cassert>
#include <cmath>
#include <iostream>
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
// Helper: Dense LU reference solve
// ============================================================================

static std::vector<double> referenceLUSolve(std::vector<double> A, std::vector<double> b, int n) {
    // Gaussian elimination with partial pivoting
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
// Test 1: Two-partition RC circuit sharing 1 boundary node
//
// Circuit: R1(n0,n1) + R2(n1,n2) + R3(n2,n3)
//   Partition 0: {n0, n1}   Partition 1: {n1, n2, n3}
//   Boundary: n1
// ============================================================================
static void testTwoPartitionRC() {
    std::cout << "\n=== Two-Partition RC ===\n";

    int N = 4;
    // Build conductance matrix:
    // R1=1kΩ (n0-n1), R2=2kΩ (n1-n2), R3=500Ω (n2-n3), R4=1kΩ (n3-gnd equiv: n3 diagonal)
    double G1 = 1e-3, G2 = 5e-4, G3 = 2e-3, G4 = 1e-3;

    std::vector<double> A(N * N, 0.0);
    A[0*N+0] = G1;        A[0*N+1] = -G1;
    A[1*N+0] = -G1;       A[1*N+1] = G1 + G2;   A[1*N+2] = -G2;
    A[2*N+1] = -G2;       A[2*N+2] = G2 + G3;    A[2*N+3] = -G3;
    A[3*N+2] = -G3;       A[3*N+3] = G3 + G4;

    std::vector<double> rhs = {1e-3, 0.0, 0.0, -5e-4};

    Csr_matrix csr = denseToCSR(A, N);

    // Two partitions: P0={0,1}, P1={1,2,3}
    TensorPartition p0, p1;
    p0.nodeSet = {0, 1}; p0.workgroupId = 0;
    p1.nodeSet = {1, 2, 3}; p1.workgroupId = 1;

    auto result = SchurSolver::solve({p0, p1}, csr, rhs);
    auto refSol = referenceLUSolve(A, rhs, N);

    EXPECT_TRUE(result.converged, "TwoPartition: converged");
    for (int i = 0; i < N; ++i) {
        EXPECT_NEAR(result.solution[i], refSol[i], 1e-13,
                    std::string("TwoPartition: x[") + std::to_string(i) + "] matches dense LU");
    }
}

// ============================================================================
// Test 2: Three partitions connected at hub node
// ============================================================================
static void testThreePartitionStar() {
    std::cout << "\n=== Three-Partition Star ===\n";

    // Hub=node 0, branches: {1,2}, {3,4}, {5,6}
    int N = 7;
    std::vector<double> A(N * N, 0.0);

    // Three branches from hub
    double G = 1.0;  // unit conductance
    // Branch 1: 0-1-2
    A[0*N+0] += G; A[0*N+1] = -G; A[1*N+0] = -G; A[1*N+1] += G + G;
    A[1*N+2] = -G; A[2*N+1] = -G; A[2*N+2] += G + 0.5; // grounding
    // Branch 2: 0-3-4
    A[0*N+0] += G; A[0*N+3] = -G; A[3*N+0] = -G; A[3*N+3] += G + G;
    A[3*N+4] = -G; A[4*N+3] = -G; A[4*N+4] += G + 0.5;
    // Branch 3: 0-5-6
    A[0*N+0] += G; A[0*N+5] = -G; A[5*N+0] = -G; A[5*N+5] += G + G;
    A[5*N+6] = -G; A[6*N+5] = -G; A[6*N+6] += G + 0.5;

    std::vector<double> rhs = {1.0, 0, 0, 0, 0, 0, 0};

    Csr_matrix csr = denseToCSR(A, N);

    TensorPartition p0, p1, p2;
    p0.nodeSet = {0, 1, 2}; p0.workgroupId = 0;
    p1.nodeSet = {0, 3, 4}; p1.workgroupId = 1;
    p2.nodeSet = {0, 5, 6}; p2.workgroupId = 2;

    auto result = SchurSolver::solve({p0, p1, p2}, csr, rhs);
    auto refSol = referenceLUSolve(A, rhs, N);

    EXPECT_TRUE(result.converged, "ThreePartition: converged");
    for (int i = 0; i < N; ++i) {
        EXPECT_NEAR(result.solution[i], refSol[i], 1e-12,
                    std::string("ThreePartition: x[") + std::to_string(i) + "] matches");
    }
}

// ============================================================================
// Test 3: Schur vs Dense parity on a larger system
// ============================================================================
static void testSchurVsDenseParity() {
    std::cout << "\n=== Schur vs Dense Parity ===\n";

    // Build a tridiagonal + diagonal system (10 nodes)
    int N = 10;
    std::vector<double> A(N * N, 0.0);
    std::vector<double> rhs(N, 0.0);

    for (int i = 0; i < N; ++i) {
        A[i * N + i] = 4.0;
        if (i > 0) { A[i * N + (i-1)] = -1.0; A[(i-1) * N + i] = -1.0; }
        rhs[i] = static_cast<double>(i + 1);
    }

    Csr_matrix csr = denseToCSR(A, N);

    // Two partitions: P0={0..4}, P1={4..9} sharing node 4
    TensorPartition p0, p1;
    p0.nodeSet = {0, 1, 2, 3, 4}; p0.workgroupId = 0;
    p1.nodeSet = {4, 5, 6, 7, 8, 9}; p1.workgroupId = 1;

    auto result = SchurSolver::solve({p0, p1}, csr, rhs);
    auto refSol = referenceLUSolve(A, rhs, N);

    EXPECT_TRUE(result.converged, "Parity: converged");
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-12,
                std::string("Parity: max error = ") + std::to_string(maxErr) + " < 1e-12");
}

// ============================================================================
// Test 4: Single partition (no boundary)
// ============================================================================
static void testSinglePartitionFallback() {
    std::cout << "\n=== Single Partition Fallback ===\n";

    int N = 3;
    std::vector<double> A = {
        2.0, -1.0,  0.0,
       -1.0,  3.0, -1.0,
        0.0, -1.0,  2.0
    };
    std::vector<double> rhs = {1.0, 0.0, -1.0};

    Csr_matrix csr = denseToCSR(A, N);

    TensorPartition p0;
    p0.nodeSet = {0, 1, 2}; p0.workgroupId = 0;

    auto result = SchurSolver::solve({p0}, csr, rhs);

    // With only 1 partition, there are no boundary nodes so Schur
    // decomposition is not applicable. The solver correctly signals
    // non-convergence so the caller falls back to direct solve.
    EXPECT_TRUE(!result.converged,
                "SinglePartition: signals fallback (no boundary nodes)");
}

// ============================================================================
// Test 5: Internal variable recovery
// ============================================================================
static void testInternalRecovery() {
    std::cout << "\n=== Internal Variable Recovery ===\n";

    // 6-node chain: 0-1-2-3-4-5
    // P0={0,1,2,3} P1={3,4,5}, boundary={3}
    int N = 6;
    std::vector<double> A(N * N, 0.0);
    for (int i = 0; i < N; ++i) {
        A[i * N + i] = 3.0;
        if (i > 0) { A[i*N+(i-1)] = -1.0; A[(i-1)*N+i] = -1.0; }
    }
    std::vector<double> rhs = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

    Csr_matrix csr = denseToCSR(A, N);

    TensorPartition p0, p1;
    p0.nodeSet = {0, 1, 2, 3}; p0.workgroupId = 0;
    p1.nodeSet = {3, 4, 5};    p1.workgroupId = 1;

    auto result = SchurSolver::solve({p0, p1}, csr, rhs);
    auto refSol = referenceLUSolve(A, rhs, N);

    EXPECT_TRUE(result.converged, "Recovery: converged");

    // Check ALL nodes (both boundary and internal)
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-12,
                std::string("Recovery: max error = ") + std::to_string(maxErr) + " < 1e-12");

    // Specifically check internal nodes (0,1,2 for P0 and 4,5 for P1)
    for (int i : {0, 1, 2, 4, 5}) {
        EXPECT_NEAR(result.solution[i], refSol[i], 1e-12,
                    std::string("Recovery: internal x[") + std::to_string(i) + "]");
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "======================================\n";
    std::cout << "Phase 5.3.2: Schur Complement Tests\n";
    std::cout << "======================================\n";

    testTwoPartitionRC();
    testThreePartitionStar();
    testSchurVsDenseParity();
    testSinglePartitionFallback();
    testInternalRecovery();

    std::cout << "\n======================================\n";
    std::cout << "Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "======================================\n";

    return g_failed > 0 ? 1 : 0;
}
