/**
 * test_graph_partitioner.cpp
 *
 * Phase C verification: GraphPartitioner unit tests.
 *
 * Verifies the three core invariants:
 *   1. Coverage  — every device index appears in exactly one partition.
 *   2. No duplicates — no device index appears twice within a partition or across
 *      partitions.
 *   3. Size bound — no partition exceeds devicesPerWorkgroup devices.
 *
 * Uses a hand-built TensorizedBlock (no file I/O, no GUI dependencies) so
 * this test is purely CPU / infrastructure-layer.
 */

#include "acutesim_engine/infrastructure/graph_partitioner.h"
#include "acutesim_engine/tensors/physics_tensors.h"

#include <cassert>
#include <iostream>
#include <unordered_set>
#include <vector>
#include <string>

// ============================================================================
// Test helpers
// ============================================================================

static int  g_passed = 0;
static int  g_failed = 0;

#define EXPECT_TRUE(expr, msg) do {                                     \
    if (!(expr)) {                                                       \
        std::cerr << "[FAIL] " << (msg) << "\n";                        \
        ++g_failed;                                                      \
    } else {                                                             \
        std::cout << "[PASS] " << (msg) << "\n";                        \
        ++g_passed;                                                      \
    }                                                                    \
} while (0)

// ============================================================================
// Core invariant checker — reused by every test case.
// ============================================================================
static bool checkPartitionInvariants(
    const std::vector<TensorPartition>& parts,
    size_t totalDevices,
    size_t devicesPerWorkgroup,
    const std::string& context)
{
    bool ok = true;

    // 1. Coverage and no-duplicate across partitions
    std::unordered_set<uint32_t> seen;
    for (const auto& p : parts) {
        for (uint32_t d : p.deviceIndices) {
            if (seen.count(d)) {
                std::cerr << "  [" << context << "] Duplicate device " << d
                          << " in partition " << p.workgroupId << "\n";
                ok = false;
            }
            seen.insert(d);
        }
        // 3. Size bound
        if (p.deviceIndices.size() > devicesPerWorkgroup) {
            std::cerr << "  [" << context << "] Partition " << p.workgroupId
                      << " has " << p.deviceIndices.size()
                      << " devices (limit " << devicesPerWorkgroup << ")\n";
            ok = false;
        }
    }

    // 2. Coverage — every device must appear
    if (seen.size() != totalDevices) {
        std::cerr << "  [" << context << "] Coverage mismatch: expected "
                  << totalDevices << ", got " << seen.size() << "\n";
        ok = false;
    }
    for (size_t i = 0; i < totalDevices; ++i) {
        if (!seen.count(static_cast<uint32_t>(i))) {
            std::cerr << "  [" << context << "] Missing device " << i << "\n";
            ok = false;
        }
    }

    return ok;
}

// ============================================================================
// Test 1: Empty netlist → empty partition list.
// ============================================================================
static void testEmpty() {
    TensorizedBlock block;
    GraphPartitioner gp;
    auto parts = gp.partition(block, 64);
    EXPECT_TRUE(parts.empty(), "Empty netlist → empty partition list");
}

// ============================================================================
// Test 2: Diode chain — 10 diodes in series.
// Node layout: 0-1-2-3-...-10 (ground at node 0)
// Expected: all diodes in one partition (fits within 64).
// ============================================================================
static void testDiodeChain() {
    TensorizedBlock block;
    const int N = 10;
    for (int i = 0; i < N; ++i)
        block.diodes.push(i, i + 1, 1e-14, 1.0, 0.02585); // anode=i, cathode=i+1

    GraphPartitioner gp;
    constexpr size_t WG = 64;
    auto parts = gp.partition(block, WG);

    bool invariants = checkPartitionInvariants(parts, N, WG, "DiodesChain");
    EXPECT_TRUE(invariants, "Diode chain: all invariants hold");
    EXPECT_TRUE(parts.size() == 1, "Diode chain: single partition (10 < 64)");
}

// ============================================================================
// Test 3: 200 independent diodes (no shared nodes).
// Must produce ≥4 partitions (200 / 64 = 3.125 → 4).
// Each partition ≤ 64 devices.
// ============================================================================
static void testManyIndependentDiodes() {
    TensorizedBlock block;
    const int N = 200;
    // Unique node pairs: (2i, 2i+1) — no sharing.
    for (int i = 0; i < N; ++i)
        block.diodes.push(2 * i, 2 * i + 1, 1e-14, 1.0, 0.02585);

    GraphPartitioner gp;
    constexpr size_t WG = 64;
    auto parts = gp.partition(block, WG);

    bool invariants = checkPartitionInvariants(parts, N, WG, "ManyIndependentDiodes");
    EXPECT_TRUE(invariants, "200 independent diodes: all invariants hold");
    EXPECT_TRUE(parts.size() >= 4,
                "200 independent diodes: at least 4 partitions produced");
}

// ============================================================================
// Test 4: Mixed device types — diodes + MOSFETs + BJTs sharing nodes.
// Ensures flat-index encoding handles cross-type adjacency correctly.
// ============================================================================
static void testMixedDevices() {
    TensorizedBlock block;

    // 5 diodes: nodes 1-2, 2-3, 3-4, 4-5, 5-6 (chained)
    for (int i = 1; i <= 5; ++i)
        block.diodes.push(i, i + 1, 1e-14, 1.0, 0.02585);

    // 3 MOSFETs sharing node 3 (gate) with the diode chain
    for (int k = 0; k < 3; ++k)
        block.mosfets.push(10 + k, /*gate*/ 3, 0, 0,
                           1e-6, 0.5e-6); // d,g,s,b, W, L

    // 2 BJTs sharing node 4 (collector) with the diode chain
    block.bjts.push(/*C*/4, /*B*/7, /*E*/8, 1e-15, 100.0, 5.0, 0.02585, true);
    block.bjts.push(/*C*/4, /*B*/9, /*E*/0, 1e-15, 100.0, 5.0, 0.02585, true);

    const size_t totalDevs = block.diodes.size() + block.mosfets.size() + block.bjts.size();
    GraphPartitioner gp;
    constexpr size_t WG = 64;
    auto parts = gp.partition(block, WG);

    bool invariants = checkPartitionInvariants(parts, totalDevs, WG, "MixedDevices");
    EXPECT_TRUE(invariants, "Mixed devices: all invariants hold");
    // All 10 devices share nodes — should fit in one or two partitions
    EXPECT_TRUE(parts.size() <= 2, "Mixed devices: ≤ 2 partitions for 10 coupled devices");
}

// ============================================================================
// Test 5: Exactly 64 diodes — single full workgroup, no spill.
// ============================================================================
static void testExactWorkgroupSize() {
    TensorizedBlock block;
    const int N = 64;
    // Chain so they are all adjacent
    for (int i = 0; i < N; ++i)
        block.diodes.push(i, i + 1, 1e-14, 1.0, 0.02585);

    GraphPartitioner gp;
    auto parts = gp.partition(block, 64);

    bool invariants = checkPartitionInvariants(parts, N, 64, "ExactWorkgroup64");
    EXPECT_TRUE(invariants, "Exactly 64 diodes: all invariants hold");
    EXPECT_TRUE(parts.size() == 1, "Exactly 64 diodes: exactly 1 partition");
    EXPECT_TRUE(parts[0].deviceIndices.size() == 64,
                "Exactly 64 diodes: partition contains 64 devices");
}

// ============================================================================
// Test 6: nodeSet is populated and contains only the unique nodes accessed.
// ============================================================================
static void testNodeSetCorrectness() {
    TensorizedBlock block;
    // 3 diodes: 1-2, 2-3, 3-4 — unique nodes: 1,2,3,4
    block.diodes.push(1, 2, 1e-14, 1.0, 0.02585);
    block.diodes.push(2, 3, 1e-14, 1.0, 0.02585);
    block.diodes.push(3, 4, 1e-14, 1.0, 0.02585);

    GraphPartitioner gp;
    auto parts = gp.partition(block, 64);

    EXPECT_TRUE(parts.size() == 1, "NodeSet: single partition for 3 diodes");
    if (!parts.empty()) {
        const auto& ns = parts[0].nodeSet;
        // Should contain nodes 1,2,3,4 (ground=0 excluded)
        EXPECT_TRUE(ns.size() == 4, "NodeSet: 4 unique non-ground nodes");
        bool hasAll = true;
        for (uint32_t n : {1u, 2u, 3u, 4u})
            if (std::find(ns.begin(), ns.end(), n) == ns.end()) { hasAll = false; break; }
        EXPECT_TRUE(hasAll, "NodeSet: contains nodes 1,2,3,4");
        // Verify sorted
        EXPECT_TRUE(std::is_sorted(ns.begin(), ns.end()),
                    "NodeSet: nodeSet is sorted");
    }
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::cout << "=== test_graph_partitioner ===\n";

    testEmpty();
    testDiodeChain();
    testManyIndependentDiodes();
    testMixedDevices();
    testExactWorkgroupSize();
    testNodeSetCorrectness();

    std::cout << "\nResults: " << g_passed << " passed, " << g_failed << " failed\n";
    return (g_failed == 0) ? 0 : 1;
}
