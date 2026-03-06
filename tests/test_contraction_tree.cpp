/**
 * test_contraction_tree.cpp — Phase 5.3.1 Contraction Tree Unit Tests
 *
 * Verifies the contraction tree structure produced by ContractionTreeBuilder.
 *
 * Test cases:
 *   1. testRCLadderTree       — 5-stage RC ladder: tree covers all devices
 *   2. testStarTree           — 4-diode star: root has max width
 *   3. testFlopEstimate       — Op-amp circuit: totalFlops > 0, peakMemory > 0
 *   4. testTreeCoversAllMPOs  — Mixed circuit: every MPO appears as exactly one leaf
 */

#include "acutesim_engine/infrastructure/contraction_tree.h"
#include "acutesim_engine/infrastructure/factor_graph.h"
#include "acutesim_engine/infrastructure/mpo_builder.h"
#include "acutesim_engine/infrastructure/treewidth_analyzer.h"
#include "acutesim_engine/tensors/physics_tensors.h"

#include <cassert>
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

// ============================================================================
// Test 1: RC Ladder Tree — 5-stage ladder
// ============================================================================
static void testRCLadderTree() {
    std::cout << "\n=== RC Ladder Contraction Tree ===\n";

    TensorizedBlock block;
    for (int i = 1; i <= 5; ++i) {
        int next = (i < 5) ? (i + 1) : 0;
        block.resistors.push(i, next, 1e3);
    }
    for (int i = 1; i <= 5; ++i) {
        block.capacitors.push(i, 0, 1e-12);
    }

    int numNodes = 6;
    auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
    auto tw = TreewidthAnalyzer::analyze(fg);
    std::vector<double> voltages(numNodes, 0.0);
    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);
    auto tree = ContractionTreeBuilder::build(fg, tw, mpos);

    EXPECT_TRUE(tree.leafIds.size() == mpos.size(),
                "RC Ladder: leaf count matches MPO count");
    EXPECT_TRUE(tree.rootId != UINT32_MAX,
                "RC Ladder: root is assigned");
    EXPECT_TRUE(tree.nodes.size() > mpos.size(),
                "RC Ladder: tree has internal nodes");

    std::cout << "  Nodes: " << tree.nodes.size()
              << "  Leaves: " << tree.leafIds.size()
              << "  Root: " << tree.rootId << "\n";
}

// ============================================================================
// Test 2: Star Tree — 4-diode star
// ============================================================================
static void testStarTree() {
    std::cout << "\n=== Star Contraction Tree ===\n";

    TensorizedBlock block;
    int hub = 1;
    for (int i = 0; i < 4; ++i) {
        block.diodes.push(hub, i + 2, 1e-14, 1.0, 0.026);
    }

    int numNodes = 6;
    auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
    auto tw = TreewidthAnalyzer::analyze(fg);
    std::vector<double> voltages(numNodes, 0.0);
    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);
    auto tree = ContractionTreeBuilder::build(fg, tw, mpos);

    EXPECT_TRUE(tree.leafIds.size() == 4, "Star: 4 leaf MPOs");
    EXPECT_TRUE(tree.rootId != UINT32_MAX, "Star: root assigned");

    std::cout << "  totalFlops=" << tree.totalFlops
              << "  peakMemory=" << tree.peakMemory << "\n";
}

// ============================================================================
// Test 3: FLOP Estimate — op-amp-like circuit
// ============================================================================
static void testFlopEstimate() {
    std::cout << "\n=== FLOP Estimate ===\n";

    TensorizedBlock block;
    // 2 MOSFET pairs + load resistors
    block.mosfets.push(1, 2, 5, 0, 10e-6, 1e-6, 200e-6, 0.7, 0.02, false);
    block.mosfets.push(3, 4, 5, 0, 10e-6, 1e-6, 200e-6, 0.7, 0.02, false);
    block.resistors.push(1, 6, 10e3);
    block.resistors.push(3, 6, 10e3);
    block.resistors.push(5, 0, 1e3);

    int numNodes = 7;
    auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
    auto tw = TreewidthAnalyzer::analyze(fg);
    std::vector<double> voltages(numNodes, 0.0);
    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);
    auto tree = ContractionTreeBuilder::build(fg, tw, mpos);

    EXPECT_TRUE(tree.totalFlops > 0, "FLOP Estimate: totalFlops > 0");
    EXPECT_TRUE(tree.peakMemory > 0, "FLOP Estimate: peakMemory > 0");

    std::cout << "  totalFlops=" << tree.totalFlops
              << "  peakMemory=" << tree.peakMemory << "\n";
}

// ============================================================================
// Test 4: Tree Covers All MPOs
// ============================================================================
static void testTreeCoversAllMPOs() {
    std::cout << "\n=== Tree Covers All MPOs ===\n";

    TensorizedBlock block;
    // Mixed circuit: resistors + diodes + MOSFET
    block.resistors.push(1, 2, 1e3);
    block.resistors.push(2, 0, 2e3);
    block.diodes.push(1, 3, 1e-14, 1.0, 0.026);
    block.mosfets.push(3, 2, 0, 0, 10e-6, 1e-6, 200e-6, 0.7, 0.02, false);

    int numNodes = 4;
    auto fg = FactorGraph::fromTensorizedBlock(block, static_cast<uint32_t>(numNodes));
    auto tw = TreewidthAnalyzer::analyze(fg);
    std::vector<double> voltages(numNodes, 0.0);
    auto mpos = MPOBuilder::buildMPOs(block, voltages, 300.15);
    auto tree = ContractionTreeBuilder::build(fg, tw, mpos);

    // Every MPO should appear as exactly one leaf
    EXPECT_TRUE(tree.leafIds.size() == mpos.size(),
                "Covers: leaf count == MPO count");

    // Verify leaf IDs are unique
    std::set<uint32_t> leafSet(tree.leafIds.begin(), tree.leafIds.end());
    EXPECT_TRUE(leafSet.size() == tree.leafIds.size(),
                "Covers: all leaf IDs are unique");

    // Verify each leaf has UINT32_MAX children (is actually a leaf)
    for (uint32_t lid : tree.leafIds) {
        EXPECT_TRUE(tree.nodes[lid].leftChild == UINT32_MAX &&
                    tree.nodes[lid].rightChild == UINT32_MAX,
                    "Covers: leaf node has no children");
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "======================================\n";
    std::cout << "Phase 5.3.1: Contraction Tree Tests\n";
    std::cout << "======================================\n";

    testRCLadderTree();
    testStarTree();
    testFlopEstimate();
    testTreeCoversAllMPOs();

    std::cout << "\n======================================\n";
    std::cout << "Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "======================================\n";

    return g_failed > 0 ? 1 : 0;
}
