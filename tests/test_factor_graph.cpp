/**
 * test_factor_graph.cpp
 *
 * Phase A verification: FactorGraph and TreewidthAnalyzer unit tests.
 *
 * Uses a hand-built TensorizedBlock (no file I/O, no GUI dependencies).
 * Follows the exact pattern of test_graph_partitioner.cpp.
 *
 * Test cases:
 *   1. testEmptyBlock          — empty tensors → FactorGraph empty, isAnalyzed=false
 *   2. testRCLadder            — 3-stage RC chain → primal path → treewidth=1
 *   3. testStarTopology        — 4 diodes sharing one node → star → treewidth=1
 *   4. testDiodeRing           — 5 diodes in a ring → 5-cycle → treewidth=2
 *   5. testOpAmpLike           — 2 MOSFETs + 3 resistors → treewidth bounded ≤ 5
 *   6. testEliminationOrdering — 4-node path → ordering covers all variables exactly once
 */

#include "acutesim_engine/infrastructure/factor_graph.h"
#include "acutesim_engine/infrastructure/treewidth_analyzer.h"
#include "acutesim_engine/tensors/physics_tensors.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>
#include <vector>
#include <string>

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
// Test 1: Empty TensorizedBlock → FactorGraph empty, isAnalyzed = false.
// ============================================================================
static void testEmptyBlock()
{
    TensorizedBlock block;
    FactorGraph fg = FactorGraph::fromTensorizedBlock(block, 0);

    EXPECT_TRUE(fg.empty(),           "Empty: FactorGraph.empty() == true");
    EXPECT_TRUE(fg.variableCount() == 0, "Empty: no variables");
    EXPECT_TRUE(fg.factorCount()   == 0, "Empty: no factors");

    TreewidthAnalysis tw = TreewidthAnalyzer::analyze(fg);
    EXPECT_TRUE(!tw.isAnalyzed, "Empty: isAnalyzed == false");
    EXPECT_TRUE(tw.estimatedTreewidth == 0, "Empty: estimatedTreewidth == 0");
}

// ============================================================================
// Test 2: 3-stage RC ladder (nodes 1–2–3).
//
// Resistors:  R1(1,2), R2(2,3), R3(3,0)
// Capacitors: C1(1,0), C2(2,0), C3(3,0)
//
// Non-ground voltage nodes: {1, 2, 3}  (no branch-current vars — no vsrc/ind)
// Primal graph: path 1–2–3   →   treewidth = 1
// ============================================================================
static void testRCLadder()
{
    TensorizedBlock block;
    block.resistors.push(1, 2, 1e3);
    block.resistors.push(2, 3, 1e3);
    block.resistors.push(3, 0, 1e3);
    block.capacitors.push(1, 0, 1e-6);
    block.capacitors.push(2, 0, 1e-6);
    block.capacitors.push(3, 0, 1e-6);

    FactorGraph fg = FactorGraph::fromTensorizedBlock(block, 4);
    EXPECT_TRUE(fg.variableCount() == 3, "RCLadder: 3 voltage variables");
    EXPECT_TRUE(fg.numBranchCurrents == 0, "RCLadder: no branch-current vars");

    TreewidthAnalysis tw = TreewidthAnalyzer::analyze(fg);
    EXPECT_TRUE(tw.isAnalyzed, "RCLadder: isAnalyzed == true");
    EXPECT_TRUE(tw.isAcyclic,  "RCLadder: primal graph is a forest");
    EXPECT_TRUE(tw.estimatedTreewidth == 1,
                "RCLadder: treewidth == 1 (path graph)");
    EXPECT_TRUE(tw.eliminationOrdering.size() == 3,
                "RCLadder: eliminationOrdering covers 3 variables");
}

// ============================================================================
// Test 3: Star topology — 4 diodes sharing one central node (node 5).
//
// D1(1,5), D2(2,5), D3(3,5), D4(4,5)
//
// Primal graph: star with centre 5, leaves {1,2,3,4} → treewidth = 1
// ============================================================================
static void testStarTopology()
{
    TensorizedBlock block;
    for (int leaf = 1; leaf <= 4; ++leaf)
        block.diodes.push(leaf, 5, 1e-14, 1.0, 0.02585);

    FactorGraph fg = FactorGraph::fromTensorizedBlock(block, 6);
    EXPECT_TRUE(fg.variableCount() == 5, "Star: 5 voltage variables (nodes 1–5)");

    TreewidthAnalysis tw = TreewidthAnalyzer::analyze(fg);
    EXPECT_TRUE(tw.isAnalyzed, "Star: isAnalyzed == true");
    EXPECT_TRUE(tw.isAcyclic,  "Star: primal graph is a forest");
    EXPECT_TRUE(tw.estimatedTreewidth == 1,
                "Star: treewidth == 1 (star is a tree)");
}

// ============================================================================
// Test 4: Diode ring — 5 diodes forming a 5-cycle.
//
// D1(1,2), D2(2,3), D3(3,4), D4(4,5), D5(5,1)
//
// Primal graph: 5-cycle   →   treewidth = 2
// ============================================================================
static void testDiodeRing()
{
    TensorizedBlock block;
    const int N = 5;
    for (int i = 0; i < N; ++i)
        block.diodes.push(i + 1, (i + 1) % N + 1, 1e-14, 1.0, 0.02585);

    FactorGraph fg = FactorGraph::fromTensorizedBlock(block, N + 1);
    EXPECT_TRUE(fg.variableCount() == 5, "DiodeRing: 5 voltage variables");

    TreewidthAnalysis tw = TreewidthAnalyzer::analyze(fg);
    EXPECT_TRUE(tw.isAnalyzed,   "DiodeRing: isAnalyzed == true");
    EXPECT_TRUE(!tw.isAcyclic,   "DiodeRing: primal graph has a cycle");
    EXPECT_TRUE(tw.estimatedTreewidth == 2,
                "DiodeRing: treewidth == 2 (5-cycle upper bound)");
}

// ============================================================================
// Test 5: Op-amp-like diff pair — 2 MOSFETs + 3 resistors.
//
// M1: D=1, G=3, S=5, B=0   (NMOS, tail node=5)
// M2: D=2, G=4, S=5, B=0
// R1: (0,1)  load 1
// R2: (0,2)  load 2
// R3: (5,0)  tail resistor
//
// No voltage sources or inductors → no branch-current vars.
// estimatedTreewidth bounded ≤ 5.
// ============================================================================
static void testOpAmpLike()
{
    TensorizedBlock block;
    block.mosfets.push(/*d*/1, /*g*/3, /*s*/5, /*b*/0, 1e-6, 0.5e-6);
    block.mosfets.push(/*d*/2, /*g*/4, /*s*/5, /*b*/0, 1e-6, 0.5e-6);
    block.resistors.push(0, 1, 5e3);
    block.resistors.push(0, 2, 5e3);
    block.resistors.push(5, 0, 2e3);

    FactorGraph fg = FactorGraph::fromTensorizedBlock(block, 6);
    EXPECT_TRUE(fg.numBranchCurrents == 0,
                "OpAmpLike: no branch-current vars (no vsrc/inductor)");
    // Voltage nodes: 1, 2, 3, 4, 5  (ground=0 excluded)
    EXPECT_TRUE(fg.variableCount() == 5, "OpAmpLike: 5 voltage variables");

    TreewidthAnalysis tw = TreewidthAnalyzer::analyze(fg);
    EXPECT_TRUE(tw.isAnalyzed, "OpAmpLike: isAnalyzed == true");
    EXPECT_TRUE(tw.estimatedTreewidth <= 5,
                "OpAmpLike: treewidth bounded ≤ 5");
}

// ============================================================================
// Test 6: Voltage source introduces a branch-current MNA variable.
//
// Simple voltage divider with an explicit voltage source:
//   Vs: (+)=1, (-)=0   →   branch-current var id = numCircuitNodes
//   R1: (1,2)
//   R2: (2,0)
//
// Variables: voltage nodes {1, 2}  +  branch-current {3}  → 3 total
// ============================================================================
static void testVoltageSourceBranchCurrent()
{
    TensorizedBlock block;
    block.voltageSources.push(1, 0, 5.0);
    block.resistors.push(1, 2, 1e3);
    block.resistors.push(2, 0, 1e3);

    // numCircuitNodes = 3 (nodes 0, 1, 2)
    FactorGraph fg = FactorGraph::fromTensorizedBlock(block, 3);

    EXPECT_TRUE(fg.numBranchCurrents == 1,
                "VsrcBranch: 1 branch-current variable for voltage source");
    // voltage nodes: {1, 2}, branch currents: {3}
    EXPECT_TRUE(fg.variableCount() == 3,
                "VsrcBranch: 3 MNA variables total (2 voltage + 1 current)");

    // Verify the branch-current variable has id = 3 (= numCircuitNodes)
    bool foundBranch = false;
    for (const auto& v : fg.variables)
        if (v.id == 3 && v.type == VariableType::BranchCurrent)
            foundBranch = true;
    EXPECT_TRUE(foundBranch, "VsrcBranch: branch-current variable has id=3");

    TreewidthAnalysis tw = TreewidthAnalyzer::analyze(fg);
    EXPECT_TRUE(tw.isAnalyzed, "VsrcBranch: isAnalyzed == true");
    EXPECT_TRUE(tw.eliminationOrdering.size() == 3,
                "VsrcBranch: ordering covers all 3 MNA variables");
}

// ============================================================================
// (Renamed from the original plan's testEliminationOrdering)
// Test the ordering invariant on a 4-node path graph (3 resistors).
//
// Path: 1–2–3–4   (no vsrc/ind → no branch-current vars)
// eliminationOrdering must be a permutation of {1,2,3,4}.
// ============================================================================
static void testEliminationOrdering()
{
    TensorizedBlock block;
    block.resistors.push(1, 2, 1e3);
    block.resistors.push(2, 3, 1e3);
    block.resistors.push(3, 4, 1e3);

    FactorGraph fg = FactorGraph::fromTensorizedBlock(block, 5);
    EXPECT_TRUE(fg.variableCount() == 4, "EliminOrder: 4 variables");

    TreewidthAnalysis tw = TreewidthAnalyzer::analyze(fg);
    EXPECT_TRUE(tw.isAnalyzed, "EliminOrder: isAnalyzed == true");
    EXPECT_TRUE(tw.eliminationOrdering.size() == fg.variableCount(),
                "EliminOrder: ordering length == variableCount");

    // Ordering must be a permutation of {1,2,3,4}
    std::unordered_set<uint32_t> seen(tw.eliminationOrdering.begin(),
                                       tw.eliminationOrdering.end());
    EXPECT_TRUE(seen.size() == fg.variableCount(),
                "EliminOrder: all variables appear exactly once");
    bool allPresent = seen.count(1) && seen.count(2) && seen.count(3) && seen.count(4);
    EXPECT_TRUE(allPresent, "EliminOrder: variables 1,2,3,4 all in ordering");
}

// ============================================================================
// main
// ============================================================================
int main()
{
    std::cout << "=== test_factor_graph ===\n";

    testEmptyBlock();
    testRCLadder();
    testStarTopology();
    testDiodeRing();
    testOpAmpLike();
    testVoltageSourceBranchCurrent();
    testEliminationOrdering();

    std::cout << "\nResults: " << g_passed << " passed, " << g_failed << " failed\n";
    return (g_failed == 0) ? 0 : 1;
}
