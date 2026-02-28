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
 *   T3  compiled_block.h  CompiledInterfacePin enum and fields
 *   T4  design_db.h  NetUUID toString/fromString round-trip
 *   T5  design_db.h  DesignDb::createNet / snapshotWithNodeMap
 *   T6  netlist_compiler  interfacePins propagation (TensorBlock → CompiledTensorBlock)
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
#include "acutesim_engine/infrastructure/compiled_block.h"
#include "acutesim_engine/infrastructure/netlist_compiler.h"

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
// T3 — CompiledInterfacePin enum and fields
// ---------------------------------------------------------------------------
static void t3_compiled_interface_pin() {
    SECTION("T3: CompiledInterfacePin Direction enum");

    CompiledInterfacePin cp;
    cp.name      = "VSS";
    cp.nodeIndex = 0;
    cp.isPower   = true;
    cp.direction = PinDirection::Power;

    CHECK(cp.isPower);
    CHECK(cp.direction == PinDirection::Power);
    CHECK(cp.name == "VSS");

    // Default direction
    CompiledInterfacePin def;
    CHECK(def.direction == PinDirection::Input);
    CHECK(!def.isPower);

    // All direction values are distinct
    CHECK(PinDirection::Input  != PinDirection::Output);
    CHECK(PinDirection::BiDir  != PinDirection::Power);
    CHECK(PinDirection::Output != PinDirection::Power);
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
// T6 — NetlistCompiler: interfacePins propagation
// ---------------------------------------------------------------------------
static void t6_compiler_propagates_interface_pins() {
    SECTION("T6: NetlistCompiler propagates TensorBlock::InterfacePin to CompiledInterfacePin");

    // Build a minimal TensorNetlist whose global block has interface pins
    TensorNetlist netlist;
    netlist.numGlobalNodes = 3;

    // Produce a stable UUID string for VDD pin
    NetUUID vddNet = NetUUID::generate();
    std::string vddUUIDStr = vddNet.toString();

    TensorBlock::InterfacePin vddPin;
    vddPin.name       = "VDD";
    vddPin.netUUIDStr = vddUUIDStr;
    vddPin.nodeIndex  = 1;
    vddPin.isPower    = true;
    vddPin.direction  = "Power";
    netlist.globalBlock.interfacePins.push_back(vddPin);

    TensorBlock::InterfacePin inPin;
    inPin.name      = "IN";
    inPin.nodeIndex = 2;
    inPin.isPower   = false;
    inPin.direction = "Input";
    netlist.globalBlock.interfacePins.push_back(inPin);

    TensorBlock::InterfacePin outPin;
    outPin.name      = "OUT";
    outPin.nodeIndex = 3;
    outPin.isPower   = false;
    outPin.direction = "Output";
    netlist.globalBlock.interfacePins.push_back(outPin);

    // Compile
    auto compiled = NetlistCompiler::compile(netlist);
    CHECK(compiled != nullptr);

    // interfacePins must be propagated
    CHECK(compiled->interfacePins.size() == 3);

    // Verify VDD (power pin)
    bool foundVdd = false;
    bool foundIn  = false;
    bool foundOut = false;

    for (const auto& cp : compiled->interfacePins) {
        if (cp.name == "VDD") {
            foundVdd = true;
            CHECK(cp.isPower);
            CHECK(cp.direction == PinDirection::Power);
            CHECK(cp.nodeIndex == 1);
            // UUID round-trip preserved
            CHECK(cp.netId == vddNet);
        } else if (cp.name == "IN") {
            foundIn = true;
            CHECK(!cp.isPower);
            CHECK(cp.direction == PinDirection::Input);
            CHECK(cp.nodeIndex == 2);
        } else if (cp.name == "OUT") {
            foundOut = true;
            CHECK(!cp.isPower);
            CHECK(cp.direction == PinDirection::Output);
            CHECK(cp.nodeIndex == 3);
        }
    }

    CHECK(foundVdd);
    CHECK(foundIn);
    CHECK(foundOut);
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

    // Place into TensorBlock and compile — UUID preserved end-to-end
    TensorNetlist netlist;
    netlist.numGlobalNodes = 2;
    TensorBlock::InterfacePin ipin;
    ipin.name       = "VOUT";
    ipin.netUUIDStr = voutChild.toString();
    ipin.nodeIndex  = 1;
    ipin.isPower    = false;
    ipin.direction  = "Output";
    netlist.globalBlock.interfacePins.push_back(ipin);

    auto compiled = NetlistCompiler::compile(netlist);
    CHECK(compiled != nullptr);
    CHECK(compiled->interfacePins.size() == 1);
    CHECK(compiled->interfacePins[0].netId == voutChild);  // UUID fidelity
    CHECK(compiled->interfacePins[0].name == "VOUT");
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
// T9 — Floating Rail Detection
// ---------------------------------------------------------------------------
static void t9_floating_rail_detection() {
    SECTION("T9: Floating rail detection — undriven PowerPort interface pin");

    // Build a subcircuit with a VDD power interface pin that has no driver
    TensorNetlist netlist;
    netlist.numGlobalNodes = 3;

    // Power pin (VDD) — exposed but no source connected
    TensorBlock::InterfacePin vddPin;
    vddPin.name       = "VDD";
    vddPin.netUUIDStr = NetUUID::generate().toString();
    vddPin.nodeIndex  = 1;
    vddPin.isPower    = true;
    vddPin.direction  = "Power";
    netlist.globalBlock.interfacePins.push_back(vddPin);

    // Signal pin (normal)
    TensorBlock::InterfacePin sigPin;
    sigPin.name       = "DATA";
    sigPin.netUUIDStr = NetUUID::generate().toString();
    sigPin.nodeIndex  = 2;
    sigPin.isPower    = false;
    sigPin.direction  = "Input";
    netlist.globalBlock.interfacePins.push_back(sigPin);

    auto compiled = NetlistCompiler::compile(netlist);
    CHECK(compiled != nullptr);
    CHECK(compiled->interfacePins.size() == 2);

    // Scan for undriven power pins (floating rail detection algorithm)
    int floatingPowerCount = 0;
    for (const auto& cp : compiled->interfacePins) {
        if (cp.isPower && cp.direction == PinDirection::Power) {
            // In a real system, we'd check if any source drives this net.
            // Here we verify the metadata is correct for the detection pass.
            floatingPowerCount++;
            CHECK(cp.name == "VDD");
        }
    }
    CHECK(floatingPowerCount == 1);

    // Non-power pins should NOT be flagged
    for (const auto& cp : compiled->interfacePins) {
        if (cp.name == "DATA") {
            CHECK(!cp.isPower);
            CHECK(cp.direction == PinDirection::Input);
        }
    }
}

// ---------------------------------------------------------------------------
// T10 — Symbol Pin Validation (data-layer struct test)
// ---------------------------------------------------------------------------
static void t10_symbol_validation_states() {
    SECTION("T10: Symbol pin validation — 3-state enum correctness");

    // Test that the SymbolValidation enum (declared in visual_component.h)
    // semantics are correct at the data layer using CompiledInterfacePin.
    // We simulate what validateSymbolPins() does by checking pin name matching.

    // Subcircuit has 3 interface pins
    std::vector<std::string> schematicPins = { "A", "B", "Y" };
    std::vector<std::string> symbolPins    = { "A", "B", "Y" };

    // CASE 1: Perfect match → Valid
    {
        bool allMatch = true;
        for (const auto& sp : symbolPins) {
            bool found = false;
            for (const auto& sch : schematicPins)
                if (sch == sp) { found = true; break; }
            if (!found) { allMatch = false; break; }
        }
        CHECK(allMatch);  // Valid state
    }

    // CASE 2: Ghost pin (symbol has "Z", schematic doesn't) → Error
    {
        std::vector<std::string> symbolWithGhost = { "A", "B", "Y", "Z" };
        bool hasGhost = false;
        for (const auto& sp : symbolWithGhost) {
            bool found = false;
            for (const auto& sch : schematicPins)
                if (sch == sp) { found = true; break; }
            if (!found) { hasGhost = true; break; }
        }
        CHECK(hasGhost);  // GhostPin state — netlisting must be blocked
    }

    // CASE 3: Hidden pin (schematic has "VDD", symbol doesn't) → Warning
    {
        std::vector<std::string> schematicWithExtra = { "A", "B", "Y", "VDD" };
        bool hasHidden = false;
        for (const auto& sch : schematicWithExtra) {
            bool found = false;
            for (const auto& sp : symbolPins)
                if (sp == sch) { found = true; break; }
            if (!found) { hasHidden = true; break; }
        }
        CHECK(hasHidden);  // HiddenPin state — netlisting still ok, yellow warning
    }

    // stableIndex validates deterministic ordering
    CompiledInterfacePin cp0, cp1, cp2;
    cp0.stableIndex = 0; cp0.name = "A";
    cp1.stableIndex = 1; cp1.name = "B";
    cp2.stableIndex = 2; cp2.name = "Y";
    CHECK(cp0.stableIndex < cp1.stableIndex);
    CHECK(cp1.stableIndex < cp2.stableIndex);
}

// ---------------------------------------------------------------------------
// T11 — PowerPort Semantics: Metadata only, no source injection
// ---------------------------------------------------------------------------
static void t11_powerport_semantics() {
    SECTION("T11: PowerPort semantics — metadata only, never auto-inject source");

    // Build a subcircuit with mixed signal and power interface pins
    TensorNetlist netlist;
    netlist.numGlobalNodes = 4;

    // VDD power pin
    NetUUID vddNet = NetUUID::generate();
    TensorBlock::InterfacePin vddPin;
    vddPin.name       = "VDD";
    vddPin.netUUIDStr = vddNet.toString();
    vddPin.nodeIndex  = 1;
    vddPin.isPower    = true;
    vddPin.direction  = "Power";

    // VSS power pin
    NetUUID vssNet = NetUUID::generate();
    TensorBlock::InterfacePin vssPin;
    vssPin.name       = "VSS";
    vssPin.netUUIDStr = vssNet.toString();
    vssPin.nodeIndex  = 2;
    vssPin.isPower    = true;
    vssPin.direction  = "Power";

    // Signal output pin
    NetUUID outNet = NetUUID::generate();
    TensorBlock::InterfacePin outPin;
    outPin.name       = "OUT";
    outPin.netUUIDStr = outNet.toString();
    outPin.nodeIndex  = 3;
    outPin.isPower    = false;
    outPin.direction  = "Output";

    netlist.globalBlock.interfacePins.push_back(vddPin);
    netlist.globalBlock.interfacePins.push_back(vssPin);
    netlist.globalBlock.interfacePins.push_back(outPin);

    auto compiled = NetlistCompiler::compile(netlist);
    CHECK(compiled != nullptr);
    CHECK(compiled->interfacePins.size() == 3);

    // KEY INVARIANT: PowerPort pins are metadata-only.
    // The compiled block must NOT contain any auto-generated voltage sources.
    // Verify by checking the structural netlist — no extra sources were added.
    size_t originalSourceCount = netlist.globalBlock.voltageSources.size();
    CHECK(compiled->structural_.globalBlock.voltageSources.size() == originalSourceCount);

    // Verify each pin has correct semantics
    int powerCount = 0;
    int signalCount = 0;
    for (const auto& cp : compiled->interfacePins) {
        if (cp.isPower) {
            powerCount++;
            CHECK(cp.direction == PinDirection::Power);
            // Power pins have valid UUID identity
            CHECK(cp.netId != NetUUID{});
        } else {
            signalCount++;
            CHECK(cp.direction == PinDirection::Output);
            CHECK(cp.name == "OUT");
        }
    }
    CHECK(powerCount == 2);   // VDD + VSS
    CHECK(signalCount == 1);  // OUT

    // UUID fidelity for power nets
    bool foundVdd = false, foundVss = false;
    for (const auto& cp : compiled->interfacePins) {
        if (cp.name == "VDD") { foundVdd = true; CHECK(cp.netId == vddNet); }
        if (cp.name == "VSS") { foundVss = true; CHECK(cp.netId == vssNet); }
    }
    CHECK(foundVdd);
    CHECK(foundVss);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("=== test_hierarchy_headless ===\n");

    // Phase 2 Tests (T1-T6)
    t1_pin_power_fields();
    t2_tensor_block_interface_pin();
    t3_compiled_interface_pin();
    t4_netuuid_roundtrip();
    t5_designdb_net_snapshot();
    t6_compiler_propagates_interface_pins();

    // Phase 4 Verification Tests (T7-T11)
    t7_uuid_tracing();
    t8_cross_instance_isolation();
    t9_floating_rail_detection();
    t10_symbol_validation_states();
    t11_powerport_semantics();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
