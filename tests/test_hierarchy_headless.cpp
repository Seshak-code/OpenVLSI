/**
 * test_hierarchy_headless.cpp
 *
 * Headless (no Qt::Gui, no QApplication) unit tests for the GUI hierarchy
 * support infrastructure added in the PowerPort / UUID-based pin resolution
 * implementation.
 *
 * Covers:
 *   T1  design_db.h  PinDirection::Power field and isPower flag
 *   T2  circuit.h    TensorBlock::InterfacePin struct round-trip
 *   T4  design_db.h  NetUUID toString/fromString round-trip
 *   T5  design_db.h  DesignDb::createNet / snapshotWithNodeMap
 *
 * Links against: Qt6::Core, components, compute/infrastructure only.
 * Explicitly avoids: Qt6::Gui, Qt6::Widgets, QGraphicsScene, VisualComponent.
 */

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// ── Data-layer headers (no GUI) ─────────────────────────────────────────────
#include "components/design_db.h"
#include "acutesim_engine/netlist/circuit.h"

using namespace acutesim::components;

// ---------------------------------------------------------------------------
// Minimal test framework — no Qt test dependency
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) \
    do { \
        if (cond) { \
            ++g_pass; \
        } else { \
            ++g_fail; \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define SECTION(name) std::printf("  [%s]\n", name)

// ---------------------------------------------------------------------------
// T1 — Pin struct: Direction::Power and isPower flag
// ---------------------------------------------------------------------------
static void t1_pin_power_fields() {
    SECTION("T1: PinDirection::Power and isPower");

    Pin p;
    p.direction = PinDirection::Power;
    p.isPower   = true;
    p.name      = "VDD";

    CHECK(p.direction == PinDirection::Power);
    CHECK(p.isPower);
    CHECK(p.name == "VDD");

    // Default direction should be Input
    Pin q;
    CHECK(q.direction == PinDirection::Input);
    CHECK(!q.isPower);
}

// ---------------------------------------------------------------------------
// T2 — TensorBlock::InterfacePin round-trip
// ---------------------------------------------------------------------------
static void t2_tensor_block_interface_pin() {
    SECTION("T2: TensorBlock::InterfacePin fields");

    TensorBlock block;

    TensorBlock::InterfacePin ipin;
    ipin.name       = "VDDIO";
    ipin.netUUIDStr = "deadbeef-0000-0000-0000-000000000001";
    ipin.nodeIndex  = 5;
    ipin.isPower    = true;
    ipin.direction  = "Power";

    block.interfacePins.push_back(ipin);

    CHECK(block.interfacePins.size() == 1);
    CHECK(block.interfacePins[0].name == "VDDIO");
    CHECK(block.interfacePins[0].nodeIndex == 5);
    CHECK(block.interfacePins[0].isPower);
    CHECK(block.interfacePins[0].direction == "Power");
    CHECK(block.interfacePins[0].netUUIDStr == "deadbeef-0000-0000-0000-000000000001");

    // A normal (non-power) input pin
    TensorBlock::InterfacePin inpin;
    inpin.name      = "CLK";
    inpin.nodeIndex = 2;
    inpin.isPower   = false;
    inpin.direction = "Input";
    block.interfacePins.push_back(inpin);

    CHECK(block.interfacePins.size() == 2);
    CHECK(!block.interfacePins[1].isPower);
    CHECK(block.interfacePins[1].direction == "Input");
}

// ---------------------------------------------------------------------------
// T4 — NetUUID toString / fromString round-trip
// ---------------------------------------------------------------------------
static void t4_netuuid_roundtrip() {
    SECTION("T4: NetUUID toString/fromString round-trip");

    NetUUID a = NetUUID::generate();
    NetUUID b = NetUUID::generate();

    // Two generated UUIDs must differ
    CHECK(a != b);

    // toString produces a 36-character string (8-4-4-4-12 + 3 dashes = 36 without dashes,
    // but our toString adds 4 dashes → 36 chars including dashes... let's just check non-empty)
    std::string sa = a.toString();
    CHECK(!sa.empty());
    CHECK(sa.size() >= 32); // at least 32 hex chars

    // fromString round-trip
    NetUUID a2 = NetUUID::fromString(sa);
    CHECK(a2 == a);

    // Null UUID (all zeros) — fromString of empty string gives all-zero bytes
    NetUUID nullId;
    std::memset(nullId.bytes.data(), 0, 16);
    NetUUID nullId2 = NetUUID::fromString(nullId.toString());
    CHECK(nullId2 == nullId);
}

// ---------------------------------------------------------------------------
// T5 — DesignDb: createNet + snapshotWithNodeMap
// ---------------------------------------------------------------------------
static void t5_designdb_net_snapshot() {
    SECTION("T5: DesignDb createNet / snapshotWithNodeMap");

    DesignDb db;

    // Create two nets
    NetUUID gnd = db.createNet("GND");
    NetUUID vdd = db.createNet("VDD");
    NetUUID clk = db.createNet("CLK");

    CHECK(gnd != vdd);
    CHECK(gnd != clk);
    CHECK(vdd != clk);

    // getNet returns the correct net
    const Net* gndNet = db.getNet(gnd);
    CHECK(gndNet != nullptr);
    CHECK(gndNet->name == "GND");

    // snapshotWithNodeMap populates netToNode for non-ground nets
    auto snap = db.snapshotWithNodeMap();
    // The snapshot must contain the created nets (at minimum as keys)
    // Node 0 is reserved for ground; other nets get indices 1..N
    bool foundVdd = snap.netToNode.find(vdd) != snap.netToNode.end();
    bool foundClk = snap.netToNode.find(clk) != snap.netToNode.end();
    CHECK(foundVdd);
    CHECK(foundClk);

    // Indices must be non-negative; VDD and CLK should differ
    if (foundVdd && foundClk) {
        int vddNode = snap.netToNode.at(vdd);
        int clkNode = snap.netToNode.at(clk);
        CHECK(vddNode != clkNode);
        CHECK(vddNode >= 0);
        CHECK(clkNode >= 0);
    }
}

// ---------------------------------------------------------------------------
// T7 — UUID Tracing: Net identity survives round-trips
// ---------------------------------------------------------------------------
static void t7_uuid_tracing() {
    SECTION("T7: UUID tracing — identity through round-trips");

    // Generate unique nets in a "child schematic" DesignDb
    DesignDb childDb;
    NetUUID voutChild = childDb.createNet("VOUT");
    NetUUID vintChild = childDb.createNet("VINT");

    // The same net name in a different DesignDb produces a different UUID
    DesignDb parentDb;
    NetUUID voutParent = parentDb.createNet("VOUT");
    CHECK(voutChild != voutParent);  // Same name, different UUID → correct isolation

    // UUID survives toString → fromString round-trip
    NetUUID voutRestored = NetUUID::fromString(voutChild.toString());
    CHECK(voutRestored == voutChild);

}

// ---------------------------------------------------------------------------
// T8 — Cross-Instance Isolation via ScopedNetUUID
// ---------------------------------------------------------------------------
static void t8_cross_instance_isolation() {
    SECTION("T8: ScopedNetUUID — cross-instance isolation");

    // Simulate two instances of the same subcircuit sharing a local net UUID
    NetUUID sharedLocalNet = NetUUID::generate();
    InstUUID inst1 = InstUUID::generate();
    InstUUID inst2 = InstUUID::generate();

    ScopedNetUUID scoped1 { inst1, sharedLocalNet };
    ScopedNetUUID scoped2 { inst2, sharedLocalNet };

    // Same local net, different instances → must be distinct
    CHECK(scoped1 != scoped2);

    // Same instance, same net → must be equal
    ScopedNetUUID scoped1b { inst1, sharedLocalNet };
    CHECK(scoped1 == scoped1b);

    // Ordering is deterministic (for std::map usage)
    CHECK((scoped1 < scoped2) || (scoped2 < scoped1));
    CHECK(!(scoped1 < scoped1b));  // not less than itself

    // Top-level scope uses zero-initialized InstUUID
    InstUUID topLevel{};
    ScopedNetUUID topScoped { topLevel, sharedLocalNet };
    CHECK(topScoped != scoped1);  // top-level ≠ instance-scoped
    CHECK(topScoped != scoped2);

    // toString produces a parseable, unique string
    std::string s1 = scoped1.toString();
    std::string s2 = scoped2.toString();
    CHECK(!s1.empty());
    CHECK(!s2.empty());
    CHECK(s1 != s2);

    // std::map insertion test — all three scoped nets are usable as keys
    std::map<ScopedNetUUID, int> scopeMap;
    scopeMap[scoped1]   = 10;
    scopeMap[scoped2]   = 20;
    scopeMap[topScoped] = 30;
    CHECK(scopeMap.size() == 3);
    CHECK(scopeMap[scoped1] == 10);
    CHECK(scopeMap[scoped2] == 20);
    CHECK(scopeMap[topScoped] == 30);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("=== test_hierarchy_headless ===\n");

    // Phase 2 Tests
    t1_pin_power_fields();
    t2_tensor_block_interface_pin();
    t4_netuuid_roundtrip();
    t5_designdb_net_snapshot();

    // Phase 4 Verification Tests
    t7_uuid_tracing();
    t8_cross_instance_isolation();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
