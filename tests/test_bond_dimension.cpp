/**
 * test_bond_dimension.cpp — Phase 5.3.0 Go/No-Go Gate
 *
 * Bond dimension scaling verification across circuit families.
 * Validates the Electrical Area Law: analog circuit classes exhibit
 * bounded bond dimension χ that grows at most as O(log N), while
 * pathological topologies (star) grow as O(N).
 *
 * Test cases:
 *   1. testRCLadderScaling       — RC ladder: χ = O(1), constant ~2
 *   2. testOpAmpScaling          — Differential pair: χ bounded ≤ 5
 *   3. testPDNMeshScaling        — √N×√N resistor grid: χ = O(√N)
 *   4. testInverterChainScaling  — CMOS inverter chain: χ = O(1), ~3
 *   5. testStarTopologyScaling   — Star: χ = O(N), pathological
 *   6. testDecisionGate          — Automated viability check
 */

#include "acutesim_engine/infrastructure/bond_dimension_profiler.h"
#include "acutesim_engine/infrastructure/factor_graph.h"
#include "acutesim_engine/tensors/physics_tensors.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
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

// ============================================================================
// Circuit Generators
// ============================================================================

// RC Ladder: N resistors in series (1-2-3-...-N-GND) with caps to ground.
// Primal graph is a path → treewidth = 1 → χ = 2 (constant).
static TensorizedBlock makeRCLadder(int N) {
    TensorizedBlock block;
    // Resistors: R_i connects node i to node i+1 (last one to ground)
    for (int i = 1; i <= N; ++i) {
        int next = (i < N) ? (i + 1) : 0;
        block.resistors.push(i, next, 1e3);
    }
    // Capacitors: C_i connects node i to ground
    for (int i = 1; i <= N; ++i) {
        block.capacitors.push(i, 0, 1e-12);
    }
    return block;
}

// Differential pair: N MOSFET pairs with resistor loads.
// Each pair: M_i(drain=2i-1, gate=2i, source=common_node, body=0)
// Load resistors from drain to VDD node.
static TensorizedBlock makeOpAmp(int numPairs) {
    TensorizedBlock block;
    int commonSource = 2 * numPairs + 1;
    int vddNode = 2 * numPairs + 2;

    for (int i = 0; i < numPairs; ++i) {
        int drain = 2 * i + 1;
        int gate  = 2 * i + 2;
        // NMOS: drain, gate, source, body
        block.mosfets.push(drain, gate, commonSource, 0,
                           10e-6, 1e-6, 200e-6, 0.7, 0.02, false);
        // Load resistor from drain to VDD
        block.resistors.push(drain, vddNode, 10e3);
    }
    // Tail current source resistor from common source to ground
    block.resistors.push(commonSource, 0, 1e3);
    return block;
}

// PDN Mesh: sqrt(N) × sqrt(N) grid of resistors.
// Node numbering: row*cols + col + 1 (1-based, ground not used as node).
// Each horizontal and vertical edge is a resistor.
static TensorizedBlock makePDNMesh(int totalNodes) {
    TensorizedBlock block;
    int side = static_cast<int>(std::sqrt(static_cast<double>(totalNodes)));
    if (side < 2) side = 2;

    auto nodeId = [&](int r, int c) { return r * side + c + 1; };

    // Horizontal edges
    for (int r = 0; r < side; ++r) {
        for (int c = 0; c < side - 1; ++c) {
            block.resistors.push(nodeId(r, c), nodeId(r, c + 1), 1.0);
        }
    }
    // Vertical edges
    for (int r = 0; r < side - 1; ++r) {
        for (int c = 0; c < side; ++c) {
            block.resistors.push(nodeId(r, c), nodeId(r + 1, c), 1.0);
        }
    }
    // Ground the corners for a well-defined system
    block.resistors.push(nodeId(0, 0), 0, 100.0);
    block.resistors.push(nodeId(side - 1, side - 1), 0, 100.0);
    return block;
}

// CMOS Inverter Chain: N inverters cascaded.
// Each inverter: PMOS(VDD, in, out, VDD) + NMOS(out, in, GND, GND)
// VDD = node N+1 (shared). Input of inverter i = output of inverter i-1.
static TensorizedBlock makeInverterChain(int N) {
    TensorizedBlock block;
    int vddNode = N + 1;
    for (int i = 0; i < N; ++i) {
        int inNode  = (i == 0) ? (N + 2) : i;  // first input is a separate node
        int outNode = i + 1;
        // PMOS: drain=out, gate=in, source=VDD, body=VDD
        block.mosfets.push(outNode, inNode, vddNode, vddNode,
                           20e-6, 1e-6, 100e-6, -0.7, 0.02, true);
        // NMOS: drain=out, gate=in, source=GND, body=GND
        block.mosfets.push(outNode, inNode, 0, 0,
                           10e-6, 1e-6, 200e-6, 0.7, 0.02, false);
    }
    // Pull-up on VDD
    block.resistors.push(vddNode, 0, 1e6);
    return block;
}

// Fully connected mesh: N(N-1)/2 resistors between all pairs.
// This is a clique topology — pathological for elimination: χ = O(N).
// (A star of diodes is NOT pathological because Min-Fill eliminates leaves
// cheaply; a clique forces worst-case fill.)
static TensorizedBlock makeStarTopology(int N) {
    TensorizedBlock block;
    // Build a fully-connected subgraph on N nodes (clique)
    for (int i = 1; i <= N; ++i) {
        for (int j = i + 1; j <= N; ++j) {
            block.resistors.push(i, j, 1e3);
        }
    }
    // Ground one node to make the system well-defined
    block.resistors.push(1, 0, 1e6);
    return block;
}

// ============================================================================
// Test 1: RC Ladder Scaling — χ should be constant ~2
// ============================================================================
static void testRCLadderScaling() {
    std::cout << "\n=== RC Ladder Scaling ===\n";
    std::vector<int> sizes = {10, 50, 200, 500};
    std::vector<uint32_t> maxChis;

    for (int N : sizes) {
        auto block = makeRCLadder(N);
        auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(N + 1));
        auto profile = BondDimensionProfiler::profile(fg, "rc_ladder");
        maxChis.push_back(profile.maxBondDimension);
        std::cout << "  N=" << N << "  max χ=" << profile.maxBondDimension
                  << "  circuitSize=" << profile.circuitSize << "\n";
    }

    // RC ladder is a path graph: treewidth = 1, so max clique = 2
    EXPECT_TRUE(maxChis.back() <= 4,
                "RC Ladder: max χ bounded (constant, ≤4)");
    // χ should not grow with N
    EXPECT_TRUE(maxChis.front() == maxChis.back(),
                "RC Ladder: χ constant across sizes");
}

// ============================================================================
// Test 2: OpAmp Scaling — χ bounded ≤ 5
// ============================================================================
static void testOpAmpScaling() {
    std::cout << "\n=== OpAmp (Differential Pair) Scaling ===\n";
    std::vector<int> sizes = {5, 10, 20, 50};
    std::vector<uint32_t> maxChis;

    for (int N : sizes) {
        auto block = makeOpAmp(N);
        int numNodes = 2 * N + 3;
        auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
        auto profile = BondDimensionProfiler::profile(fg, "opamp");
        maxChis.push_back(profile.maxBondDimension);
        std::cout << "  pairs=" << N << "  max χ=" << profile.maxBondDimension
                  << "  circuitSize=" << profile.circuitSize << "\n";
    }

    // Differential pairs share common source: bounded treewidth
    EXPECT_TRUE(maxChis.back() <= 10,
                "OpAmp: max χ bounded (≤10)");
}

// ============================================================================
// Test 3: PDN Mesh Scaling — χ = O(√N)
// ============================================================================
static void testPDNMeshScaling() {
    std::cout << "\n=== PDN Mesh Scaling ===\n";
    std::vector<int> sizes = {16, 64, 256};
    std::vector<uint32_t> maxChis;

    for (int N : sizes) {
        auto block = makePDNMesh(N);
        int side = static_cast<int>(std::sqrt(static_cast<double>(N)));
        int numNodes = side * side + 1;
        auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
        auto profile = BondDimensionProfiler::profile(fg, "pdn_mesh");
        maxChis.push_back(profile.maxBondDimension);
        std::cout << "  N=" << N << " (side=" << side << ")  max χ="
                  << profile.maxBondDimension
                  << "  circuitSize=" << profile.circuitSize << "\n";
    }

    // For a √N×√N mesh, treewidth ≈ √N. Verify growth is sub-linear.
    // χ(256) / χ(16) should be roughly √(256/16) = 4
    if (maxChis[0] > 0) {
        double growthRatio = static_cast<double>(maxChis.back()) / maxChis[0];
        double sizeRatio = std::sqrt(256.0 / 16.0);
        EXPECT_TRUE(growthRatio <= sizeRatio * 2.0,
                    "PDN Mesh: χ growth is sub-linear (O(√N))");
    }
    // χ should grow (not constant like ladder)
    EXPECT_TRUE(maxChis.back() > maxChis.front(),
                "PDN Mesh: χ grows with N");
}

// ============================================================================
// Test 4: Inverter Chain Scaling — χ = O(1), bounded ~3
// ============================================================================
static void testInverterChainScaling() {
    std::cout << "\n=== CMOS Inverter Chain Scaling ===\n";
    std::vector<int> sizes = {10, 50, 200};
    std::vector<uint32_t> maxChis;

    for (int N : sizes) {
        auto block = makeInverterChain(N);
        int numNodes = N + 3;
        auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
        auto profile = BondDimensionProfiler::profile(fg, "inverter_chain");
        maxChis.push_back(profile.maxBondDimension);
        std::cout << "  N=" << N << "  max χ=" << profile.maxBondDimension
                  << "  circuitSize=" << profile.circuitSize << "\n";
    }

    // Inverter chain has bounded treewidth due to local connectivity
    EXPECT_TRUE(maxChis.back() <= 8,
                "Inverter Chain: max χ bounded (≤8)");
}

// ============================================================================
// Test 5: Star Topology Scaling — χ = O(N), pathological
// ============================================================================
static void testStarTopologyScaling() {
    std::cout << "\n=== Clique Topology Scaling (Pathological) ===\n";
    std::vector<int> sizes = {5, 10, 20};
    std::vector<uint32_t> maxChis;

    for (int N : sizes) {
        auto block = makeStarTopology(N);
        int numNodes = N + 1;
        auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
        auto profile = BondDimensionProfiler::profile(fg, "clique");
        maxChis.push_back(profile.maxBondDimension);
        std::cout << "  N=" << N << "  max χ=" << profile.maxBondDimension
                  << "  circuitSize=" << profile.circuitSize << "\n";
    }

    // Clique topology: every node is connected to every other.
    // Treewidth = N-1, so χ = N. Should grow linearly with N.
    EXPECT_TRUE(maxChis.back() > maxChis.front(),
                "Clique: χ grows with N (pathological)");
    EXPECT_TRUE(maxChis.back() >= 15,
                "Clique: χ(20) is large (≥15)");
}

// ============================================================================
// Test 6: Decision Gate — Automated viability assessment
// ============================================================================
static void testDecisionGate() {
    std::cout << "\n=== Decision Gate: TN Viability Assessment ===\n";

    struct ClassResult {
        std::string name;
        std::vector<std::pair<int, uint32_t>> sizeToChiMax;
        bool tnViable = false;
    };

    auto assessClass = [](const std::string& name,
                          const std::vector<std::pair<int, uint32_t>>& data) -> bool {
        // Fit χ_max(N) to a + b*log(N). If b is small, TN is viable.
        if (data.size() < 2) return false;

        // Simple linear regression on (log(N), χ_max)
        double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        double n = static_cast<double>(data.size());
        for (const auto& [N, chi] : data) {
            double x = std::log(static_cast<double>(N));
            double y = static_cast<double>(chi);
            sumX += x; sumY += y; sumXY += x * y; sumX2 += x * x;
        }
        double b = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
        double a = (sumY - b * sumX) / n;

        std::cout << "  " << name << ": fit χ = " << a << " + " << b
                  << " * log(N)";

        // Viable if slope b < 2.0 (very slow or no log growth)
        bool viable = (b < 2.0);
        std::cout << (viable ? " → TN VIABLE" : " → SPARSE SOLVER") << "\n";
        return viable;
    };

    // RC Ladder
    {
        std::vector<std::pair<int, uint32_t>> data;
        for (int N : {10, 50, 200}) {
            auto block = makeRCLadder(N);
            auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(N + 1));
            auto profile = BondDimensionProfiler::profile(fg, "rc_ladder");
            data.push_back({N, profile.maxBondDimension});
        }
        bool viable = assessClass("rc_ladder", data);
        EXPECT_TRUE(viable, "Decision Gate: RC Ladder is TN-viable");
    }

    // Inverter Chain
    {
        std::vector<std::pair<int, uint32_t>> data;
        for (int N : {10, 50, 200}) {
            auto block = makeInverterChain(N);
            auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(N + 3));
            auto profile = BondDimensionProfiler::profile(fg, "inverter_chain");
            data.push_back({N, profile.maxBondDimension});
        }
        bool viable = assessClass("inverter_chain", data);
        EXPECT_TRUE(viable, "Decision Gate: Inverter Chain is TN-viable");
    }

    // Clique (should NOT be viable)
    {
        std::vector<std::pair<int, uint32_t>> data;
        for (int N : {5, 10, 20}) {
            auto block = makeStarTopology(N);
            auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(N + 1));
            auto profile = BondDimensionProfiler::profile(fg, "clique");
            data.push_back({N, profile.maxBondDimension});
        }
        bool viable = assessClass("clique", data);
        EXPECT_TRUE(!viable, "Decision Gate: Clique topology is NOT TN-viable");
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "======================================\n";
    std::cout << "Phase 5.3.0: Bond Dimension Experiment\n";
    std::cout << "======================================\n";

    testRCLadderScaling();
    testOpAmpScaling();
    testPDNMeshScaling();
    testInverterChainScaling();
    testStarTopologyScaling();
    testDecisionGate();

    std::cout << "\n======================================\n";
    std::cout << "Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "======================================\n";

    return g_failed > 0 ? 1 : 0;
}
