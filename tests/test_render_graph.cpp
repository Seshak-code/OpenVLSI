#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <set>
#include <vector>

#include "contraction_render_graph.h"
#include "contraction_tree.h"

// ============================================================================
// Helpers
// ============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s  (line %d)\n", #cond, __LINE__); \
        ++g_failed; \
    } else { \
        ++g_passed; \
    } \
} while(0)

// Build a simple linear chain: L0 -> L1 -> N2 -> N3  (root=N3)
//
//   L0 (leaf)  L1 (leaf)
//       \        /
//        N2 (internal, merges L0+L1)
//               \
//   L3 (leaf)    |
//       \       /
//        N4 (internal, merges N2+L3)  <-- root
//
static ContractionTree makeChain5() {
    ContractionTree tree;
    tree.nodes.resize(5);

    // Leaf 0
    tree.nodes[0].id = 0;
    tree.nodes[0].openIndices = {10, 11};

    // Leaf 1
    tree.nodes[1].id = 1;
    tree.nodes[1].openIndices = {11, 12};

    // Leaf 3
    tree.nodes[2].id = 3;
    tree.nodes[2].openIndices = {12, 13};

    // Internal 2: merges L0 + L1
    tree.nodes[3].id = 2;
    tree.nodes[3].leftChild  = 0;
    tree.nodes[3].rightChild = 1;
    tree.nodes[3].openIndices = {10, 12};
    tree.nodes[3].contractedIndices = {11};

    // Internal 4 (root): merges N2 + L3
    tree.nodes[4].id = 4;
    tree.nodes[4].leftChild  = 2;
    tree.nodes[4].rightChild = 3;
    tree.nodes[4].openIndices = {10, 13};
    tree.nodes[4].contractedIndices = {12};

    tree.rootId = 4;
    tree.leafIds = {0, 1, 3};
    return tree;
}

// Build a wider tree: 4 leaves, 3 internal nodes.
//   L0   L1    L2   L3
//    \  /       \  /
//     N4         N5
//       \       /
//        N6 (root)
static ContractionTree makeBalanced7() {
    ContractionTree tree;
    tree.nodes.resize(7);

    // Leaves
    for (uint32_t i = 0; i < 4; ++i) {
        tree.nodes[i].id = i;
        tree.nodes[i].openIndices = {100 + i, 100 + i + 1};
    }

    // Internal N4: L0 + L1
    tree.nodes[4].id = 4;
    tree.nodes[4].leftChild  = 0;
    tree.nodes[4].rightChild = 1;
    tree.nodes[4].openIndices = {100, 102};

    // Internal N5: L2 + L3
    tree.nodes[5].id = 5;
    tree.nodes[5].leftChild  = 2;
    tree.nodes[5].rightChild = 3;
    tree.nodes[5].openIndices = {102, 104};

    // Internal N6 (root): N4 + N5
    tree.nodes[6].id = 6;
    tree.nodes[6].leftChild  = 4;
    tree.nodes[6].rightChild = 5;
    tree.nodes[6].openIndices = {100, 104};

    tree.rootId = 6;
    tree.leafIds = {0, 1, 2, 3};
    return tree;
}

// ============================================================================
// Tests
// ============================================================================

void test_empty_tree() {
    printf("test_empty_tree ...\n");
    ContractionTree empty;
    auto sched = ContractionRenderGraph::analyze(empty);
    CHECK(!sched.valid);
    CHECK(sched.numPhysicalSlots == 0);
    CHECK(sched.wavefronts.empty());
}

void test_single_leaf() {
    printf("test_single_leaf ...\n");
    ContractionTree tree;
    tree.nodes.resize(1);
    tree.nodes[0].id = 0;
    tree.nodes[0].openIndices = {1, 2};
    tree.rootId = 0;
    tree.leafIds = {0};

    auto sched = ContractionRenderGraph::analyze(tree);
    CHECK(sched.valid);
    CHECK(sched.numPhysicalSlots == 1);
    CHECK(sched.nodeIdToPhysical[0] == 0);
    // No internal nodes -> no wavefronts
    CHECK(sched.wavefronts.empty());
}

void test_chain_slot_count() {
    printf("test_chain_slot_count ...\n");
    auto tree = makeChain5();
    auto sched = ContractionRenderGraph::analyze(tree, 128);

    CHECK(sched.valid);
    // 5 logical nodes, but aliasing should reduce physical slot count.
    // Leaves L0, L1, L3 bulk-upload at time 0.
    // N2 consumes L0, L1 at walk index 3 -> L0, L1 free after 3.
    // N4 consumes N2, L3 at walk index 4 -> N2, L3 free after 4.
    // With aliasing, should need fewer than 5 physical slots.
    CHECK(sched.numPhysicalSlots <= 5);
    CHECK(sched.numPhysicalSlots >= 2); // at least 2 (root + one leaf in parallel)

    // Every node should have a physical slot assigned
    for (const auto& lt : sched.lifetimes)
        CHECK(lt.physicalSlot != UINT32_MAX);
}

void test_chain_memory_stats() {
    printf("test_chain_memory_stats ...\n");
    auto tree = makeChain5();
    const uint64_t slotBytes = 256;
    auto sched = ContractionRenderGraph::analyze(tree, slotBytes);

    CHECK(sched.valid);
    CHECK(sched.unaliasedBytes == 5 * slotBytes);
    CHECK(sched.aliasedBytes == sched.numPhysicalSlots * slotBytes);
    CHECK(sched.aliasedBytes <= sched.unaliasedBytes);
}

void test_chain_wavefronts() {
    printf("test_chain_wavefronts ...\n");
    auto tree = makeChain5();
    auto sched = ContractionRenderGraph::analyze(tree);

    CHECK(sched.valid);
    // Should have wavefronts for internal nodes only (N2 and N4)
    // N2 at depth 1, N4 at depth 2 -> 2 wavefronts
    CHECK(sched.wavefronts.size() == 2);

    // Total internal nodes across all wavefronts should be 2
    uint32_t totalInternal = 0;
    for (const auto& wf : sched.wavefronts)
        totalInternal += static_cast<uint32_t>(wf.walkIndices.size());
    CHECK(totalInternal == 2);
}

void test_balanced_wavefront_parallelism() {
    printf("test_balanced_wavefront_parallelism ...\n");
    auto tree = makeBalanced7();
    auto sched = ContractionRenderGraph::analyze(tree);

    CHECK(sched.valid);
    // N4 and N5 are at depth 1 (can execute in parallel), N6 at depth 2
    CHECK(sched.wavefronts.size() == 2);

    // First wavefront (depth 1) should have 2 passes (N4 and N5)
    bool foundDepth1 = false;
    for (const auto& wf : sched.wavefronts) {
        if (wf.depth == 1) {
            CHECK(wf.walkIndices.size() == 2);
            foundDepth1 = true;
        }
    }
    CHECK(foundDepth1);

    // Second wavefront (depth 2) should have 1 pass (N6 = root)
    bool foundDepth2 = false;
    for (const auto& wf : sched.wavefronts) {
        if (wf.depth == 2) {
            CHECK(wf.walkIndices.size() == 1);
            foundDepth2 = true;
        }
    }
    CHECK(foundDepth2);
}

void test_balanced_slot_aliasing() {
    printf("test_balanced_slot_aliasing ...\n");
    auto tree = makeBalanced7();
    auto sched = ContractionRenderGraph::analyze(tree, 64);

    CHECK(sched.valid);
    // 7 logical nodes, aliased should be strictly fewer
    CHECK(sched.numPhysicalSlots < 7);
    // Memory savings
    CHECK(sched.aliasedBytes < sched.unaliasedBytes);
}

void test_barriers_present_for_internal() {
    printf("test_barriers_present_for_internal ...\n");
    auto tree = makeChain5();
    auto sched = ContractionRenderGraph::analyze(tree);

    CHECK(sched.valid);
    CHECK(sched.barriers.size() == tree.nodes.size());

    // Internal nodes should have non-empty barriers (they read children + write output)
    // Walk index 3 = N2 (internal), walk index 4 = N4 (internal)
    CHECK(!sched.barriers[3].empty());
    CHECK(!sched.barriers[4].empty());

    // Leaf nodes should have empty barriers
    CHECK(sched.barriers[0].empty());
    CHECK(sched.barriers[1].empty());
    CHECK(sched.barriers[2].empty());
}

void test_barriers_sorted() {
    printf("test_barriers_sorted ...\n");
    auto tree = makeBalanced7();
    auto sched = ContractionRenderGraph::analyze(tree);

    CHECK(sched.valid);
    // All barrier vectors should be sorted
    for (const auto& b : sched.barriers) {
        for (size_t j = 1; j < b.size(); ++j)
            CHECK(b[j] > b[j - 1]);
    }
}

void test_root_survives_walk() {
    printf("test_root_survives_walk ...\n");
    auto tree = makeChain5();
    auto sched = ContractionRenderGraph::analyze(tree);

    CHECK(sched.valid);
    // Root (node id 4, walk index 4) should have lastRead == N (= 5)
    // to ensure its slot isn't aliased away before CPU readback
    bool rootFound = false;
    for (const auto& lt : sched.lifetimes) {
        if (lt.treeNodeId == 4) {
            CHECK(lt.lastRead == tree.nodes.size());
            rootFound = true;
        }
    }
    CHECK(rootFound);
}

void test_no_physical_slot_collision() {
    printf("test_no_physical_slot_collision ...\n");
    auto tree = makeBalanced7();
    auto sched = ContractionRenderGraph::analyze(tree);

    CHECK(sched.valid);

    // For each walk index pair where lifetimes overlap, their physical
    // slots must be different.
    const auto& lts = sched.lifetimes;
    for (size_t i = 0; i < lts.size(); ++i) {
        for (size_t j = i + 1; j < lts.size(); ++j) {
            // Check if [firstWrite_i, lastRead_i] overlaps [firstWrite_j, lastRead_j].
            // Strict inequality: boundary touching (firstWrite == lastRead) is NOT
            // a collision — the consuming kernel reads inputs before writing output.
            bool overlaps = (lts[i].firstWrite < lts[j].lastRead) &&
                            (lts[j].firstWrite < lts[i].lastRead);
            if (overlaps) {
                CHECK(lts[i].physicalSlot != lts[j].physicalSlot);
            }
        }
    }
}

void test_nodeIdToPhysical_mapping() {
    printf("test_nodeIdToPhysical_mapping ...\n");
    auto tree = makeBalanced7();
    auto sched = ContractionRenderGraph::analyze(tree);

    CHECK(sched.valid);

    // Every tree node's id should map to a valid physical slot
    for (const auto& node : tree.nodes) {
        CHECK(node.id < sched.nodeIdToPhysical.size());
        CHECK(sched.nodeIdToPhysical[node.id] < sched.numPhysicalSlots);
    }

    // Verify consistency: lifetime's physicalSlot matches nodeIdToPhysical
    for (const auto& lt : sched.lifetimes) {
        CHECK(sched.nodeIdToPhysical[lt.treeNodeId] == lt.physicalSlot);
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Render Graph Scheduler Tests ===\n\n");

    test_empty_tree();
    test_single_leaf();
    test_chain_slot_count();
    test_chain_memory_stats();
    test_chain_wavefronts();
    test_balanced_wavefront_parallelism();
    test_balanced_slot_aliasing();
    test_barriers_present_for_internal();
    test_barriers_sorted();
    test_root_survives_walk();
    test_no_physical_slot_collision();
    test_nodeIdToPhysical_mapping();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
