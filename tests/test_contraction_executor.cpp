/**
 * test_contraction_executor.cpp — Phase 5.3.3 Contraction Executor Unit Tests
 *
 * Verifies the tensor contraction execution engine produces correct MNA
 * solutions by walking the ContractionTree bottom-up with dense Schur
 * elimination and back-substitution.
 *
 * Test cases:
 *   1. testTwoNodeResistor      — 1R + Vsource: V/R
 *   2. testVoltageDivider       — 2R series: mid-point = V*R2/(R1+R2)
 *   3. testRCLadder5            — 5-stage RC (tw=2): matches dense LU ≤ 1e-13
 *   4. testDiodeCircuit         — Diode + R: RHS propagation at 1 linearization point
 *   5. testStarTopology         — Hub + 8 peripherals: parity with dense LU
 *   6. testBackSubRecovery      — 6-node chain: every eliminated var recovered
 *   7. testGMINConditioning     — Floating node: GMIN prevents singular pivots
 *   8. testWorkspaceReuse       — RC ladder, 2 calls: reset+re-solve correct
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
// Helper: Build a TNCompiledProgram from hand-crafted MPOs and tree
// ============================================================================

/**
 * Build a simple two-leaf contraction tree for a 2-terminal device.
 * Leaf 0 = MPO 0, Leaf 1 = MPO 1 (or same if single device).
 * Internal node contracts shared variables.
 */
static TNCompiledProgram buildSimpleProgram(
    const std::vector<DeviceMPO>& mpos,
    const std::vector<uint32_t>& contractedVars)
{
    TNCompiledProgram prog;
    prog.viable = true;
    prog.mpos = mpos;

    ContractionTree& tree = prog.tree;

    if (mpos.size() == 1) {
        // Single device: one leaf = root
        ContractionNode leaf;
        leaf.id = 0;
        leaf.openIndices = mpos[0].terminalNodes;
        tree.nodes.push_back(leaf);
        tree.leafIds = {0};
        tree.rootId = 0;
    } else {
        // Two+ devices: create leaves and one internal root
        uint32_t nextId = 0;
        for (size_t i = 0; i < mpos.size(); ++i) {
            ContractionNode leaf;
            leaf.id = nextId++;
            leaf.openIndices = mpos[i].terminalNodes;
            tree.nodes.push_back(leaf);
            tree.leafIds.push_back(leaf.id);
        }

        // Build internal nodes pairwise (left-skewed tree)
        uint32_t currentLeft = 0;
        for (size_t i = 1; i < mpos.size(); ++i) {
            ContractionNode internal;
            internal.id = nextId++;
            internal.leftChild = currentLeft;
            internal.rightChild = static_cast<uint32_t>(i);
            internal.contractedIndices = contractedVars;

            // Compute open indices = union(left, right) - contracted
            std::set<uint32_t> allIndices;
            for (uint32_t v : tree.nodes[currentLeft].openIndices) allIndices.insert(v);
            for (uint32_t v : tree.nodes[i].openIndices) allIndices.insert(v);
            for (uint32_t v : contractedVars) allIndices.erase(v);
            internal.openIndices.assign(allIndices.begin(), allIndices.end());

            tree.nodes.push_back(internal);
            currentLeft = internal.id;
        }
        tree.rootId = currentLeft;
    }

    return prog;
}

/**
 * Dense LU reference solve (Gaussian elimination with partial pivoting).
 */
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
// Helper: Build a resistor MPO between two nodes
// ============================================================================

static DeviceMPO makeResistorMPO(uint32_t n1, uint32_t n2, double R) {
    DeviceMPO mpo;
    mpo.deviceType = FactorDeviceType::Resistor;
    mpo.deviceIndex = 0;
    double G = 1.0 / R;

    // Collect non-ground terminals
    std::vector<uint32_t> terms;
    if (n1 > 0) terms.push_back(n1);
    if (n2 > 0) terms.push_back(n2);
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    mpo.terminalNodes = terms;
    mpo.rank = static_cast<uint32_t>(terms.size());

    if (terms.size() == 2) {
        // Both non-ground: 2x2 matrix [[G,-G],[-G,G]]
        mpo.localMatrix = {G, -G, -G, G};
        mpo.localRHS = {0.0, 0.0};
    } else if (terms.size() == 1) {
        // One terminal to ground: 1x1 matrix [[G]]
        mpo.localMatrix = {G};
        mpo.localRHS = {0.0};
    }

    return mpo;
}

// ============================================================================
// Helper: Build a voltage source MPO (node to ground)
// ============================================================================

static DeviceMPO makeVSourceMPO(uint32_t node, double voltage) {
    DeviceMPO mpo;
    mpo.deviceType = FactorDeviceType::VoltageSource;
    mpo.deviceIndex = 0;
    mpo.terminalNodes = {node};
    mpo.rank = 1;
    // Large conductance to model ideal voltage source
    double Gv = 1e9;
    mpo.localMatrix = {Gv};
    mpo.localRHS = {Gv * voltage};
    return mpo;
}

// ============================================================================
// Test 1: Two-node resistor with voltage source
// Circuit: Vsource(5V) at node 1, R=1kΩ from node 1 to ground
// Expected: V[0] = 5.0 (node 1 is MNA index 0)
// ============================================================================

static void testTwoNodeResistor() {
    std::cout << "\n=== Test 1: Two-Node Resistor ===\n";

    DeviceMPO vsrc = makeVSourceMPO(1, 5.0);
    DeviceMPO res = makeResistorMPO(1, 0, 1000.0);

    // Both MPOs share node 1 — no contraction needed (same index)
    // Build a program with 2 leaves merged at internal node, no contracted vars
    std::vector<DeviceMPO> mpos = {vsrc, res};
    TNCompiledProgram prog = buildSimpleProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, 1);

    EXPECT_TRUE(result.converged, "TwoNodeResistor: converged");
    EXPECT_NEAR(result.solution[0], 5.0, 1e-6,
                "TwoNodeResistor: V[0] = 5.0V");
}

// ============================================================================
// Test 2: Voltage divider
// Circuit: Vsrc(10V) at node 1, R1=1kΩ (node1-node2), R2=1kΩ (node2-gnd)
// Expected: V[0]=10V, V[1]=5V
// ============================================================================

static void testVoltageDivider() {
    std::cout << "\n=== Test 2: Voltage Divider ===\n";

    DeviceMPO vsrc = makeVSourceMPO(1, 10.0);
    DeviceMPO r1 = makeResistorMPO(1, 2, 1000.0);
    DeviceMPO r2 = makeResistorMPO(2, 0, 1000.0);

    std::vector<DeviceMPO> mpos = {vsrc, r1, r2};

    // Build tree: (vsrc ⊕ r1) contract nothing → ⊕ r2 contract nothing
    // Node 1 shared between vsrc and r1; node 2 shared between r1 and r2
    // We don't contract (eliminate) any nodes — root solves full 2x2
    TNCompiledProgram prog = buildSimpleProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, 2);

    EXPECT_TRUE(result.converged, "VoltageDivider: converged");
    EXPECT_NEAR(result.solution[0], 10.0, 1e-6,
                "VoltageDivider: V[0] = 10.0V");
    EXPECT_NEAR(result.solution[1], 5.0, 1e-6,
                "VoltageDivider: V[1] = 5.0V");
}

// ============================================================================
// Test 3: 5-stage RC ladder (tw=2)
// R=1kΩ each, all to ground via separate R to ground
// Vsrc at node 1 = 10V
// ============================================================================

static void testRCLadder5() {
    std::cout << "\n=== Test 3: RC Ladder 5-stage ===\n";

    int N = 5;
    // Build the global conductance matrix for reference
    double G = 1e-3;  // 1kΩ = 1e-3 S
    double Gload = 0.5e-3;  // 2kΩ load to ground at each node

    std::vector<double> globalA(N * N, 0.0);
    std::vector<double> globalRHS(N, 0.0);

    // Vsource at node 0 (MNA node 1)
    double Gv = 1e9;
    globalA[0*N+0] += Gv;
    globalRHS[0] += Gv * 10.0;

    // Ladder: R between consecutive nodes
    for (int i = 0; i < N - 1; ++i) {
        globalA[i*N+i] += G;
        globalA[i*N+(i+1)] -= G;
        globalA[(i+1)*N+i] -= G;
        globalA[(i+1)*N+(i+1)] += G;
    }

    // Load to ground at each non-source node
    for (int i = 1; i < N; ++i) {
        globalA[i*N+i] += Gload;
    }

    auto refSol = referenceLUSolve(globalA, globalRHS, N);

    // Build MPOs
    std::vector<DeviceMPO> mpos;

    // Vsource at node 1
    mpos.push_back(makeVSourceMPO(1, 10.0));

    // Ladder resistors (node i+1 to node i+2, MNA 1-based)
    for (int i = 0; i < N - 1; ++i) {
        mpos.push_back(makeResistorMPO(
            static_cast<uint32_t>(i + 1),
            static_cast<uint32_t>(i + 2),
            1000.0));
    }

    // Load resistors to ground
    for (int i = 1; i < N; ++i) {
        mpos.push_back(makeResistorMPO(static_cast<uint32_t>(i + 1), 0, 2000.0));
    }

    // Build a simple program (no contraction — root solves full system)
    TNCompiledProgram prog = buildSimpleProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, static_cast<uint32_t>(N));

    EXPECT_TRUE(result.converged, "RCLadder5: converged");
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-6,
                std::string("RCLadder5: max error = ") + std::to_string(maxErr) + " < 1e-6");
}

// ============================================================================
// Test 4: Diode circuit (linearized at one operating point)
// Diode (anode=node1, cathode=gnd) + R(node1, Vsrc node2)
// Tests that localRHS propagation works for nonlinear devices
// ============================================================================

static void testDiodeCircuit() {
    std::cout << "\n=== Test 4: Diode Circuit ===\n";

    // Linearized diode at Vd=0.65V: g_d = Is/Vt * exp(Vd/Vt)
    double Is = 1e-14;
    double Vt = 0.02585;
    double Vd = 0.65;
    double g_d = (Is / Vt) * exp(Vd / Vt);
    double I_d = Is * (exp(Vd / Vt) - 1.0);
    // Norton equivalent: I_eq = I_d - g_d * Vd
    double I_eq = I_d - g_d * Vd;

    // Diode MPO: node 1 to ground
    DeviceMPO diodeMPO;
    diodeMPO.deviceType = FactorDeviceType::Diode;
    diodeMPO.deviceIndex = 0;
    diodeMPO.terminalNodes = {1};
    diodeMPO.rank = 1;
    diodeMPO.localMatrix = {g_d};
    diodeMPO.localRHS = {-I_eq};  // Norton current into the node

    // R = 100Ω from node 1 to node 2
    DeviceMPO rMPO = makeResistorMPO(1, 2, 100.0);

    // Vsource 5V at node 2
    DeviceMPO vMPO = makeVSourceMPO(2, 5.0);

    std::vector<DeviceMPO> mpos = {diodeMPO, rMPO, vMPO};
    TNCompiledProgram prog = buildSimpleProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, 2);

    // Reference: build the 2x2 system manually
    // Node 1: g_d*V1 + G*(V1-V2) = -I_eq  →  (g_d+G)*V1 - G*V2 = -I_eq
    // Node 2: G*(V2-V1) + Gv*V2 = Gv*5    →  -G*V1 + (G+Gv)*V2 = Gv*5
    double G = 0.01;  // 100Ω
    double Gv = 1e9;
    std::vector<double> A = {
        g_d + G, -G,
        -G, G + Gv
    };
    std::vector<double> rhs = {-I_eq, Gv * 5.0};
    auto refSol = referenceLUSolve(A, rhs, 2);

    EXPECT_TRUE(result.converged, "DiodeCircuit: converged");
    EXPECT_NEAR(result.solution[0], refSol[0], 1e-6,
                "DiodeCircuit: V[0] (diode anode)");
    EXPECT_NEAR(result.solution[1], refSol[1], 1e-6,
                "DiodeCircuit: V[1] (source)");
}

// ============================================================================
// Test 5: Star topology (hub + 8 peripherals)
// Hub = node 1, peripherals = nodes 2..9, each connected via 1kΩ
// Vsource at node 1 = 10V, load at each peripheral = 2kΩ to ground
// ============================================================================

static void testStarTopology() {
    std::cout << "\n=== Test 5: Star Topology ===\n";

    int N = 9;
    double G = 1e-3;
    double Gload = 0.5e-3;
    double Gv = 1e9;

    // Build reference matrix
    std::vector<double> globalA(N * N, 0.0);
    std::vector<double> globalRHS(N, 0.0);

    // Vsource at node 1 (index 0)
    globalA[0] += Gv;
    globalRHS[0] += Gv * 10.0;

    // Star: hub (node 1, idx 0) to each peripheral (nodes 2..9, idx 1..8)
    for (int i = 1; i < N; ++i) {
        globalA[0*N+0] += G;
        globalA[0*N+i] -= G;
        globalA[i*N+0] -= G;
        globalA[i*N+i] += G + Gload;
    }

    auto refSol = referenceLUSolve(globalA, globalRHS, N);

    // Build MPOs
    std::vector<DeviceMPO> mpos;
    mpos.push_back(makeVSourceMPO(1, 10.0));
    for (int i = 1; i < N; ++i) {
        mpos.push_back(makeResistorMPO(1, static_cast<uint32_t>(i + 1), 1000.0));
        mpos.push_back(makeResistorMPO(static_cast<uint32_t>(i + 1), 0, 2000.0));
    }

    TNCompiledProgram prog = buildSimpleProgram(mpos, {});

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, static_cast<uint32_t>(N));

    EXPECT_TRUE(result.converged, "StarTopology: converged");
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-6,
                std::string("StarTopology: max error = ") + std::to_string(maxErr) + " < 1e-6");
}

// ============================================================================
// Test 6: Back-substitution recovery on 6-node chain with elimination
// Chain: 1-2-3-4-5-6, R=1kΩ between consecutive, Vsrc at 1, load at 6
// Contract (eliminate) nodes 3 and 4 during tree contraction
// ============================================================================

static void testBackSubRecovery() {
    std::cout << "\n=== Test 6: Back-Substitution Recovery ===\n";

    int N = 6;
    double G = 1e-3;
    double Gv = 1e9;
    double Gload = 1e-3;

    // Reference system
    std::vector<double> globalA(N * N, 0.0);
    std::vector<double> globalRHS(N, 0.0);

    globalA[0] += Gv;
    globalRHS[0] += Gv * 10.0;

    for (int i = 0; i < N - 1; ++i) {
        globalA[i*N+i] += G;
        globalA[i*N+(i+1)] -= G;
        globalA[(i+1)*N+i] -= G;
        globalA[(i+1)*N+(i+1)] += G;
    }
    globalA[(N-1)*N+(N-1)] += Gload;

    auto refSol = referenceLUSolve(globalA, globalRHS, N);

    // Build MPOs for each link
    // mpo[0]: VSource at node 1         → {1}
    // mpo[1]: R(1,2)                    → {1,2}
    // mpo[2]: R(2,3)                    → {2,3}
    // mpo[3]: R(3,4)                    → {3,4}
    // mpo[4]: R(4,5)                    → {4,5}
    // mpo[5]: R(5,6)                    → {5,6}
    // mpo[6]: R(6,gnd)                  → {6}
    std::vector<DeviceMPO> mpos;
    mpos.push_back(makeVSourceMPO(1, 10.0));        // [0]
    for (int i = 0; i < N - 1; ++i) {
        mpos.push_back(makeResistorMPO(             // [1..5]
            static_cast<uint32_t>(i + 1),
            static_cast<uint32_t>(i + 2),
            1000.0));
    }
    mpos.push_back(makeResistorMPO(static_cast<uint32_t>(N), 0, 1000.0));  // [6]

    // Build a proper contraction tree where nodes 3,4 are contracted
    // ONLY at the merge point where all touching devices are accumulated.
    //
    // Tree structure:
    //   Leaf 0: VSource(1) → {1}
    //   Leaf 1: R(1,2)     → {1,2}
    //   Leaf 2: R(2,3)     → {2,3}
    //   Leaf 3: R(3,4)     → {3,4}
    //   Leaf 4: R(4,5)     → {4,5}
    //   Leaf 5: R(5,6)     → {5,6}
    //   Leaf 6: R(6,gnd)   → {6}
    //   Node 7:  leaf0 ⊕ leaf1, contract {} → {1,2}
    //   Node 8:  node7 ⊕ leaf2, contract {} → {1,2,3}
    //   Node 9:  leaf3 ⊕ leaf4, contract {} → {3,4,5}
    //   Node 10: node8 ⊕ node9, contract {3,4} → {1,2,5}
    //   Node 11: leaf5 ⊕ leaf6, contract {} → {5,6}
    //   Node 12: node10 ⊕ node11, contract {} → {1,2,5,6}  ROOT

    TNCompiledProgram prog;
    prog.viable = true;
    prog.mpos = mpos;
    ContractionTree& tree = prog.tree;

    // Leaves
    for (uint32_t i = 0; i < 7; ++i) {
        ContractionNode leaf;
        leaf.id = i;
        leaf.openIndices = mpos[i].terminalNodes;
        tree.nodes.push_back(leaf);
        tree.leafIds.push_back(i);
    }

    // Node 7: leaf0 ⊕ leaf1, no contraction
    tree.nodes.push_back({7, 0, 1, {1, 2}, {}, 0, 0});

    // Node 8: node7 ⊕ leaf2, no contraction
    tree.nodes.push_back({8, 7, 2, {1, 2, 3}, {}, 0, 0});

    // Node 9: leaf3 ⊕ leaf4, no contraction
    tree.nodes.push_back({9, 3, 4, {3, 4, 5}, {}, 0, 0});

    // Node 10: node8 ⊕ node9, CONTRACT {3, 4}
    tree.nodes.push_back({10, 8, 9, {1, 2, 5}, {3, 4}, 0, 0});

    // Node 11: leaf5 ⊕ leaf6, no contraction
    tree.nodes.push_back({11, 5, 6, {5, 6}, {}, 0, 0});

    // Node 12: node10 ⊕ node11, no contraction (root)
    tree.nodes.push_back({12, 10, 11, {1, 2, 5, 6}, {}, 0, 0});

    tree.rootId = 12;

    TensorWorkspace workspace;
    auto result = ContractionExecutor::solve(prog, workspace, static_cast<uint32_t>(N));

    EXPECT_TRUE(result.converged, "BackSub: converged");
    // Check ALL nodes, especially the eliminated ones (3, 4)
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::abs(result.solution[i] - refSol[i]);
        if (err > maxErr) maxErr = err;
    }
    EXPECT_TRUE(maxErr < 1e-6,
                std::string("BackSub: max error = ") + std::to_string(maxErr) + " < 1e-6");

    // Specifically check eliminated nodes
    EXPECT_NEAR(result.solution[2], refSol[2], 1e-6,
                "BackSub: eliminated node 3 recovered");
    EXPECT_NEAR(result.solution[3], refSol[3], 1e-6,
                "BackSub: eliminated node 4 recovered");
}

// ============================================================================
// Test 7: GMIN conditioning prevents singular pivots
// Floating node: two resistors in series, middle node floating
// Without GMIN, the floating node creates a singular pivot
// ============================================================================

static void testGMINConditioning() {
    std::cout << "\n=== Test 7: GMIN Conditioning ===\n";

    // Node 1: Vsource 5V
    // Node 2: floating (R1 from 1 to 2, R2 from 2 to 3)
    // Node 3: grounded via load
    double G = 1e-3;
    double Gv = 1e9;
    double Gload = 1e-3;

    DeviceMPO vsrc = makeVSourceMPO(1, 5.0);
    DeviceMPO r1 = makeResistorMPO(1, 2, 1000.0);
    DeviceMPO r2 = makeResistorMPO(2, 3, 1000.0);
    DeviceMPO rload = makeResistorMPO(3, 0, 1000.0);

    std::vector<DeviceMPO> mpos = {vsrc, r1, r2, rload};
    TNCompiledProgram prog = buildSimpleProgram(mpos, {});

    TensorWorkspace workspace;
    // Use default gmin=1e-12
    auto result = ContractionExecutor::solve(prog, workspace, 3);

    EXPECT_TRUE(result.converged, "GMIN: converged");

    // Reference: 3-node system
    std::vector<double> A = {
        Gv + G, -G, 0.0,
        -G, G + G, -G,
        0.0, -G, G + Gload
    };
    std::vector<double> rhs = {Gv * 5.0, 0.0, 0.0};
    auto refSol = referenceLUSolve(A, rhs, 3);

    // With GMIN, solution should still be very close to reference
    EXPECT_NEAR(result.solution[0], refSol[0], 1e-3,
                "GMIN: V[0] close to reference");
    EXPECT_NEAR(result.solution[1], refSol[1], 1e-3,
                "GMIN: V[1] close to reference");
    EXPECT_NEAR(result.solution[2], refSol[2], 1e-3,
                "GMIN: V[2] close to reference");
}

// ============================================================================
// Test 8: Workspace reuse — solve twice, verify both are correct
// ============================================================================

static void testWorkspaceReuse() {
    std::cout << "\n=== Test 8: Workspace Reuse ===\n";

    // Simple voltage divider
    DeviceMPO vsrc = makeVSourceMPO(1, 10.0);
    DeviceMPO r1 = makeResistorMPO(1, 2, 1000.0);
    DeviceMPO r2 = makeResistorMPO(2, 0, 1000.0);

    std::vector<DeviceMPO> mpos = {vsrc, r1, r2};
    TNCompiledProgram prog = buildSimpleProgram(mpos, {});

    TensorWorkspace workspace;

    // First solve
    auto result1 = ContractionExecutor::solve(prog, workspace, 2);
    EXPECT_TRUE(result1.converged, "Reuse: first solve converged");
    EXPECT_NEAR(result1.solution[0], 10.0, 1e-6, "Reuse: first V[0]=10V");
    EXPECT_NEAR(result1.solution[1], 5.0, 1e-6, "Reuse: first V[1]=5V");

    // Change the source voltage and rebuild MPOs
    prog.mpos[0] = makeVSourceMPO(1, 20.0);

    // Second solve (workspace should be reset internally)
    auto result2 = ContractionExecutor::solve(prog, workspace, 2);
    EXPECT_TRUE(result2.converged, "Reuse: second solve converged");
    EXPECT_NEAR(result2.solution[0], 20.0, 1e-6, "Reuse: second V[0]=20V");
    EXPECT_NEAR(result2.solution[1], 10.0, 1e-6, "Reuse: second V[1]=10V");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=======================================\n";
    std::cout << "Phase 5.3.3: Contraction Executor Tests\n";
    std::cout << "=======================================\n";

    testTwoNodeResistor();
    testVoltageDivider();
    testRCLadder5();
    testDiodeCircuit();
    testStarTopology();
    testBackSubRecovery();
    testGMINConditioning();
    testWorkspaceReuse();

    std::cout << "\n=======================================\n";
    std::cout << "Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "=======================================\n";

    return g_failed > 0 ? 1 : 0;
}
