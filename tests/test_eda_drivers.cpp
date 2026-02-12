/**
 * test_eda_drivers.cpp — Unit tests for EDA tool drivers
 *
 * Tests Yosys TCL generation, Yosys log parsing, OpenROAD TCL generation,
 * OpenROAD report parsing, DEF coordinate translation, and KLayout DRC/LVS
 * report parsing.
 *
 * All tests are pure unit tests using hardcoded golden strings.
 * NO external tools (Yosys, OpenROAD) are required.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <QString>
#include <QPointF>
#include <QSizeF>

#include "compute/orchestration/drivers/yosys_task.h"
#include "compute/orchestration/drivers/yosys_log_parser.h"
#include "compute/orchestration/drivers/openroad_task.h"
#include "compute/orchestration/drivers/openroad_report_parser.h"
#include "compute/orchestration/drivers/klayout_report_parser.h"

using namespace acutesim::compute::orchestration;

// ============================================================================
// HELPERS
// ============================================================================

#define ASSERT_CONTAINS(haystack, needle) \
    do { \
        if (!(haystack).contains(needle)) { \
            std::cerr << "FAIL: Expected to find \"" << qPrintable(QString(needle)) \
                      << "\" in output.\n" \
                      << "Output was:\n" << qPrintable(haystack) << "\n"; \
            assert(false && "ASSERT_CONTAINS failed"); \
        } \
    } while (0)

#define ASSERT_NOT_CONTAINS(haystack, needle) \
    do { \
        if ((haystack).contains(needle)) { \
            std::cerr << "FAIL: Did NOT expect \"" << qPrintable(QString(needle)) \
                      << "\" in output.\n"; \
            assert(false && "ASSERT_NOT_CONTAINS failed"); \
        } \
    } while (0)

#define ASSERT_NEAR(actual, expected, tol) \
    do { \
        if (std::abs((actual) - (expected)) > (tol)) { \
            std::cerr << "FAIL: " << (actual) << " != " << (expected) \
                      << " (tolerance: " << (tol) << ")\n"; \
            assert(false && "ASSERT_NEAR failed"); \
        } \
    } while (0)

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct Register_##name { \
        Register_##name() { \
            try { \
                test_##name(); \
                std::cout << "[PASS] " #name "\n"; \
                testsPassed++; \
            } catch (...) { \
                std::cerr << "[FAIL] " #name "\n"; \
                testsFailed++; \
            } \
        } \
    } register_##name; \
    static void test_##name()

// ============================================================================
// YOSYS TCL GENERATION TESTS
// ============================================================================

TEST(yosys_tcl_area_goal) {
    SynthesisConfig config;
    config.topModule = "counter";
    config.sourceFiles = {"counter.v"};
    config.targetLibrary = "sky130_fd_sc_hd";
    config.goal = SynthesisGoal::AREA;
    
    QString tcl = YosysTask::generateTcl(config);
    
    ASSERT_CONTAINS(tcl, "read_liberty -lib sky130_fd_sc_hd.lib");
    ASSERT_CONTAINS(tcl, "read_verilog counter.v");
    ASSERT_CONTAINS(tcl, "hierarchy -check -top counter");
    ASSERT_CONTAINS(tcl, "proc; opt; fsm; opt; memory; opt");
    ASSERT_CONTAINS(tcl, "techmap; opt");
    ASSERT_CONTAINS(tcl, "abc -liberty sky130_fd_sc_hd.lib");
    ASSERT_CONTAINS(tcl, "map,-a");  // Area optimization flag
    ASSERT_CONTAINS(tcl, "dfflibmap -liberty sky130_fd_sc_hd.lib");
    ASSERT_CONTAINS(tcl, "write_verilog -noattr syn_counter.v");
    ASSERT_CONTAINS(tcl, "stat -liberty sky130_fd_sc_hd.lib");
}

TEST(yosys_tcl_speed_goal) {
    SynthesisConfig config;
    config.topModule = "alu";
    config.sourceFiles = {"alu.v", "adder.v"};
    config.targetLibrary = "sky130_fd_sc_hd";
    config.goal = SynthesisGoal::SPEED;
    
    QString tcl = YosysTask::generateTcl(config);
    
    ASSERT_CONTAINS(tcl, "read_verilog alu.v");
    ASSERT_CONTAINS(tcl, "read_verilog adder.v");
    ASSERT_CONTAINS(tcl, "map,-p");  // Speed optimization flag
    ASSERT_NOT_CONTAINS(tcl, "map,-a");  // Should NOT contain area flag
}

TEST(yosys_tcl_power_goal) {
    SynthesisConfig config;
    config.topModule = "sensor_ctrl";
    config.sourceFiles = {"sensor_ctrl.v"};
    config.targetLibrary = "sky130_fd_sc_hd";
    config.goal = SynthesisGoal::POWER;
    
    QString tcl = YosysTask::generateTcl(config);
    
    ASSERT_CONTAINS(tcl, "scorr");   // Power-specific optimization
    ASSERT_CONTAINS(tcl, "dc2");     // Power-specific optimization
}

TEST(yosys_tcl_balanced_goal) {
    SynthesisConfig config;
    config.topModule = "top";
    config.sourceFiles = {"top.v"};
    config.goal = SynthesisGoal::BALANCED;
    
    QString tcl = YosysTask::generateTcl(config);
    
    // Balanced should NOT have specific optimization scripts
    ASSERT_NOT_CONTAINS(tcl, "map,-a");
    ASSERT_NOT_CONTAINS(tcl, "map,-p");
    ASSERT_NOT_CONTAINS(tcl, "scorr");
}

TEST(yosys_json_tcl) {
    SynthesisConfig config;
    config.topModule = "top";
    config.sourceFiles = {"top.v"};
    config.targetLibrary = "sky130_fd_sc_hd";
    
    QString tcl = YosysTask::generateJsonTcl(config);
    
    ASSERT_CONTAINS(tcl, "stat -json -liberty");
    ASSERT_CONTAINS(tcl, "JSON Output Mode");
}

TEST(yosys_batch_command) {
    QString cmd = YosysTask::generateBatchCommand("/tmp/script.tcl");
    assert(cmd == "yosys -s /tmp/script.tcl");
}

// ============================================================================
// YOSYS LOG PARSER TESTS
// ============================================================================

TEST(yosys_parse_single_module) {
    QString golden = R"(
-- Running command `stat -liberty sky130_fd_sc_hd.lib' --

=== inverter ===

   Number of wires:                 3
   Number of wire bits:             3
   Number of public wires:          2
   Number of cells:                 2
     $_NOT_                          1
     sky130_fd_sc_hd__inv_1          1

   Chip area for module '\inverter': 5.382400

)";
    
    SynthesisResult result = YosysLogParser::parse(golden);
    
    assert(result.success);
    assert(result.cellCount == 2);
    assert(result.wireCount == 3);
    ASSERT_NEAR(result.estimatedArea, 5.3824, 0.001);
    
    // Per-module check
    assert(result.modules.contains("inverter"));
    const auto& mod = result.modules["inverter"];
    assert(mod.moduleName == "inverter");
    assert(mod.cellCount == 2);
    assert(mod.wireCount == 3);
    assert(mod.wireBits == 3);
    assert(mod.publicWires == 2);
    assert(mod.cellBreakdown.contains("$_NOT_"));
    assert(mod.cellBreakdown["$_NOT_"] == 1);
    assert(mod.cellBreakdown.contains("sky130_fd_sc_hd__inv_1"));
    assert(mod.cellBreakdown["sky130_fd_sc_hd__inv_1"] == 1);
}

TEST(yosys_parse_multi_module) {
    QString golden = R"(
=== adder ===

   Number of wires:                10
   Number of cells:                 5

=== counter ===

   Number of wires:                25
   Number of cells:                12
     $_DFF_P_                        4
     $_NOT_                          3
     sky130_fd_sc_hd__and2_1         5

   Chip area for module '\counter': 100.500000

)";
    
    SynthesisResult result = YosysLogParser::parse(golden);
    
    assert(result.success);
    // Top-level should be last module (counter)
    assert(result.cellCount == 12);
    assert(result.wireCount == 25);
    ASSERT_NEAR(result.estimatedArea, 100.5, 0.001);
    
    // Per-module
    assert(result.modules.size() == 2);
    assert(result.modules.contains("adder"));
    assert(result.modules.contains("counter"));
    assert(result.modules["adder"].cellCount == 5);
    assert(result.modules["counter"].cellCount == 12);
}

TEST(yosys_parse_with_errors) {
    QString golden = R"(
ERROR: Module 'missing_module' not found!
Warning: Unused wire 'x' in module 'top'.
)";
    
    SynthesisResult result = YosysLogParser::parse(golden);
    
    assert(!result.success);
    assert(result.errors.size() >= 1);
    assert(result.warnings.size() >= 1);
    assert(result.errors[0].contains("missing_module"));
}

TEST(yosys_parse_empty_output) {
    SynthesisResult result = YosysLogParser::parse("");
    assert(result.success);  // No errors = success
    assert(result.cellCount == 0);
}

TEST(yosys_isSuccessful) {
    assert(YosysLogParser::isSuccessful("All good\nstat done\n"));
    assert(!YosysLogParser::isSuccessful("Some output\nERROR: bad thing\n"));
}

// ============================================================================
// OPENROAD TCL GENERATION TESTS
// ============================================================================

TEST(openroad_floorplan_tcl) {
    FloorplanConfig config;
    config.designName = "counter";
    config.dieArea = QSizeF(100.0, 100.0);
    config.techLef = "sky130_tech.lef";
    config.cellLefs = {"sky130_fd_sc_hd.lef"};
    config.synthesizedNetlist = "syn_counter.v";
    
    QString tcl = OpenRoadTask::generateFloorplanTcl(config);
    
    ASSERT_CONTAINS(tcl, "read_lef sky130_tech.lef");
    ASSERT_CONTAINS(tcl, "read_lef sky130_fd_sc_hd.lef");
    ASSERT_CONTAINS(tcl, "read_verilog syn_counter.v");
    ASSERT_CONTAINS(tcl, "link_design counter");
    ASSERT_CONTAINS(tcl, "initialize_floorplan -die_area");
    ASSERT_CONTAINS(tcl, "place_pins");
}

TEST(openroad_placement_tcl) {
    PlacementConfig config;
    config.designName = "counter";
    config.goal = PlacementGoal::TIMING;
    config.targetDensity = 0.7;
    
    QString tcl = OpenRoadTask::generatePlacementTcl(config);
    
    ASSERT_CONTAINS(tcl, "global_placement");
    ASSERT_CONTAINS(tcl, "0.7");
    ASSERT_CONTAINS(tcl, "-timing_driven");
    ASSERT_CONTAINS(tcl, "detailed_placement");
    ASSERT_CONTAINS(tcl, "check_placement");
}

TEST(openroad_cts_tcl) {
    CTSConfig config;
    config.clockNet = "clk";
    config.bufferCell = "sky130_fd_sc_hd__clkbuf_4";
    
    QString tcl = OpenRoadTask::generateCTSTcl(config);
    
    ASSERT_CONTAINS(tcl, "clock_tree_synthesis");
    ASSERT_CONTAINS(tcl, "sky130_fd_sc_hd__clkbuf_4");
    ASSERT_CONTAINS(tcl, "repair_clock_nets");
}

TEST(openroad_routing_tcl) {
    RoutingConfig config;
    config.designName = "counter";
    config.effort = RoutingEffort::HIGH;
    config.repairAntennas = true;
    
    QString tcl = OpenRoadTask::generateRoutingTcl(config);
    
    ASSERT_CONTAINS(tcl, "global_route");
    ASSERT_CONTAINS(tcl, "detailed_route");
    ASSERT_CONTAINS(tcl, "repair_antennas");
    ASSERT_CONTAINS(tcl, "check_routing");
}

TEST(openroad_full_flow_tcl) {
    FloorplanConfig fpConfig;
    fpConfig.designName = "counter";
    fpConfig.dieArea = QSizeF(200, 200);
    fpConfig.techLef = "tech.lef";
    fpConfig.synthesizedNetlist = "syn.v";
    
    PlacementConfig plConfig;
    plConfig.designName = "counter";
    
    CTSConfig ctsConfig;
    ctsConfig.clockNet = "clk";
    
    RoutingConfig rtConfig;
    rtConfig.designName = "counter";
    
    QString tcl = OpenRoadTask::generateFullFlowTcl(fpConfig, plConfig, ctsConfig, rtConfig);
    
    ASSERT_CONTAINS(tcl, "PHASE 1: FLOORPLAN");
    ASSERT_CONTAINS(tcl, "PHASE 2: PLACEMENT");
    ASSERT_CONTAINS(tcl, "PHASE 3: CLOCK TREE SYNTHESIS");
    ASSERT_CONTAINS(tcl, "PHASE 4: ROUTING");
    ASSERT_CONTAINS(tcl, "report_design_area");
    ASSERT_CONTAINS(tcl, "report_power");
    ASSERT_CONTAINS(tcl, "report_worst_slack");
    ASSERT_CONTAINS(tcl, "write_def counter_final.def");
}

// ============================================================================
// OPENROAD REPORT PARSER TESTS
// ============================================================================

TEST(openroad_parse_design_area) {
    QString golden = "Design area 12345.678 u^2 67% utilization.\n";
    auto result = OpenRoadReportParser::parseDesignArea(golden);
    
    ASSERT_NEAR(result.totalArea, 12345.678, 0.01);
    ASSERT_NEAR(result.utilization, 0.67, 0.01);
}

TEST(openroad_parse_timing_clean) {
    QString golden = R"(
worst slack 0.500
tns 0.000
)";
    auto result = OpenRoadReportParser::parseTiming(golden);
    
    ASSERT_NEAR(result.wns, 0.5, 0.001);
    ASSERT_NEAR(result.tns, 0.0, 0.001);
    assert(result.timingClean);
}

TEST(openroad_parse_timing_violation) {
    QString golden = R"(
worst slack -0.123
tns -4.567
Failing endpoints: 3
)";
    auto result = OpenRoadReportParser::parseTiming(golden);
    
    ASSERT_NEAR(result.wns, -0.123, 0.001);
    ASSERT_NEAR(result.tns, -4.567, 0.001);
    assert(result.setupViolations == 3);
    assert(!result.timingClean);
}

TEST(openroad_parse_power) {
    QString golden = R"(
Total Power     = 1.234 mW
Dynamic Power   = 0.567 mW
Leakage Power   = 0.012 mW
Internal Power  = 0.345 mW
)";
    auto result = OpenRoadReportParser::parsePower(golden);
    
    ASSERT_NEAR(result.totalPower, 1.234, 0.001);
    ASSERT_NEAR(result.dynamicPower, 0.567, 0.001);
    ASSERT_NEAR(result.leakagePower, 0.012, 0.001);
    ASSERT_NEAR(result.internalPower, 0.345, 0.001);
}

TEST(openroad_parse_routing_clean) {
    QString golden = R"(
Number of routed nets: 1234
Number of unrouted nets: 0
DRC violations: 0
Antenna violations: 0
)";
    auto result = OpenRoadReportParser::parseRouting(golden);
    
    assert(result.routedNets == 1234);
    assert(result.unroutedNets == 0);
    assert(result.drcViolations == 0);
    assert(result.drcClean);
}

TEST(openroad_parse_routing_violations) {
    QString golden = R"(
Number of routed nets: 1200
Number of unrouted nets: 34
DRC violations: 5
Antenna violations: 2
)";
    auto result = OpenRoadReportParser::parseRouting(golden);
    
    assert(result.routedNets == 1200);
    assert(result.unroutedNets == 34);
    assert(result.drcViolations == 5);
    assert(result.antennaViolations == 2);
    assert(!result.drcClean);
}

TEST(openroad_parse_full_output) {
    QString golden = R"(
OpenROAD v2.0
Design area 500.123 u^2 45% utilization.
worst slack 0.200
tns 0.000
Total Power     = 2.500 mW
Dynamic Power   = 1.800 mW
Leakage Power   = 0.100 mW
Internal Power  = 0.600 mW
Number of routed nets: 500
DRC violations: 0
[WARNING] Clock tree has high skew
)";
    auto result = OpenRoadReportParser::parse(golden);
    
    assert(result.success);
    ASSERT_NEAR(result.placement.totalArea, 500.123, 0.01);
    ASSERT_NEAR(result.timing.wns, 0.2, 0.001);
    assert(result.timingClean());
    assert(result.drcClean());
    ASSERT_NEAR(result.power.totalPower, 2.5, 0.01);
    assert(result.warnings.size() == 1);
}

TEST(openroad_parse_with_errors) {
    QString golden = "[ERROR] Cannot read LEF file: missing.lef\n";
    PnRResult result;
    OpenRoadReportParser::parseErrors(golden, result);
    
    assert(result.errors.size() == 1);
    assert(result.errors[0].contains("missing.lef"));
}

// ============================================================================
// DEF TRANSLATION TESTS
// ============================================================================

TEST(def_coordinate_flip) {
    // Canvas: upper-left origin, component at (100, 200) in 1000px canvas
    // DEF:   lower-left origin
    // With scale 0.1 um/px and 1000 units/um:
    // defX = 100 * 0.1 * 1000 = 10000
    // defY = (1000 - 200) * 0.1 * 1000 = 80000
    
    QList<SchematicPlacement> placements;
    SchematicPlacement p;
    p.instanceName = "U1";
    p.cellName = "sky130_fd_sc_hd__inv_1";
    p.canvasPosition = QPointF(100.0, 200.0);
    p.rotation = 0.0;
    p.mirrored = false;
    placements.append(p);
    
    QString def = DEFTranslator::generateDEF(
        "test_design", placements,
        QSizeF(100.0, 100.0),  // 100x100 um die
        0.1, 1000.0, 1000);
    
    ASSERT_CONTAINS(def, "DESIGN test_design");
    ASSERT_CONTAINS(def, "DIEAREA ( 0 0 ) ( 100000 100000 )");
    ASSERT_CONTAINS(def, "COMPONENTS 1");
    ASSERT_CONTAINS(def, "U1 sky130_fd_sc_hd__inv_1");
    ASSERT_CONTAINS(def, "10000 80000");  // Y is flipped
    ASSERT_CONTAINS(def, ") N ;");  // Normal orientation
}

TEST(def_orientation_mapping) {
    assert(DEFTranslator::toDefOrientation(0.0, false) == "N");
    assert(DEFTranslator::toDefOrientation(90.0, false) == "E");
    assert(DEFTranslator::toDefOrientation(180.0, false) == "S");
    assert(DEFTranslator::toDefOrientation(270.0, false) == "W");
    assert(DEFTranslator::toDefOrientation(0.0, true) == "FN");
    assert(DEFTranslator::toDefOrientation(90.0, true) == "FE");
    assert(DEFTranslator::toDefOrientation(180.0, true) == "FS");
    assert(DEFTranslator::toDefOrientation(270.0, true) == "FW");
    // Negative rotation
    assert(DEFTranslator::toDefOrientation(-90.0, false) == "W");
}

TEST(def_multiple_components) {
    QList<SchematicPlacement> placements;
    
    SchematicPlacement p1;
    p1.instanceName = "U1";
    p1.cellName = "sky130_fd_sc_hd__inv_1";
    p1.canvasPosition = QPointF(0, 0);
    placements.append(p1);
    
    SchematicPlacement p2;
    p2.instanceName = "U2";
    p2.cellName = "sky130_fd_sc_hd__nand2_1";
    p2.canvasPosition = QPointF(500, 500);
    p2.rotation = 90.0;
    placements.append(p2);
    
    SchematicPlacement p3;
    p3.instanceName = "U3";
    p3.cellName = "sky130_fd_sc_hd__buf_2";
    p3.canvasPosition = QPointF(250, 750);
    p3.mirrored = true;
    placements.append(p3);
    
    QString def = DEFTranslator::generateDEF(
        "multi_test", placements,
        QSizeF(200.0, 200.0), 0.1, 1000.0, 1000);
    
    ASSERT_CONTAINS(def, "COMPONENTS 3");
    ASSERT_CONTAINS(def, "U1 sky130_fd_sc_hd__inv_1");
    ASSERT_CONTAINS(def, "U2 sky130_fd_sc_hd__nand2_1");
    ASSERT_CONTAINS(def, "U3 sky130_fd_sc_hd__buf_2");
    ASSERT_CONTAINS(def, ") E ;");   // U2: 90°
    ASSERT_CONTAINS(def, ") FN ;");  // U3: mirrored
}

TEST(def_empty_placements) {
    QList<SchematicPlacement> empty;
    QString def = DEFTranslator::generateDEF("empty_design", empty,
                                              QSizeF(10, 10));
    ASSERT_CONTAINS(def, "COMPONENTS 0");
    ASSERT_CONTAINS(def, "END DESIGN");
}

TEST(def_header_format) {
    QList<SchematicPlacement> placements;
    QString def = DEFTranslator::generateDEF("mydesign", placements,
                                              QSizeF(50, 50));
    ASSERT_CONTAINS(def, "VERSION 5.8");
    ASSERT_CONTAINS(def, "DIVIDERCHAR \"/\"");
    ASSERT_CONTAINS(def, "BUSBITCHARS \"[]\"");
    ASSERT_CONTAINS(def, "UNITS DISTANCE MICRONS 1000");
}

// ============================================================================
// KLAYOUT DRC PARSER TESTS
// ============================================================================

TEST(klayout_drc_clean) {
    QString output = R"(
KLayout DRC Report
==================
Rule: metal1.width | Category: Width | Layer: metal1 | Violations: 0
Rule: metal1.spacing | Category: Spacing | Layer: metal1 | Violations: 0
Rule: via1.enclosure | Category: Enclosure | Layer: via1 | Violations: 0
Total DRC violations: 0
DRC clean
    )";
    auto result = KLayoutReportParser::parseDRC(output);
    assert(result.success);
    assert(result.cleanDRC);
    assert(result.totalViolations == 0);
    assert(result.violations.size() == 3); // 3 rules checked
}

TEST(klayout_drc_violations) {
    QString output = R"(
KLayout DRC Report
==================
Rule: metal1.width | Category: Width | Layer: metal1 | Violations: 5
Rule: metal1.spacing | Category: Spacing | Layer: metal1 | Violations: 3
Rule: via1.enclosure | Category: Enclosure | Layer: via1 | Violations: 0
Total DRC violations: 8
    )";
    auto result = KLayoutReportParser::parseDRC(output);
    assert(result.success);
    assert(!result.cleanDRC);
    assert(result.totalViolations == 8);
    assert(result.errorCount == 8);
    assert(result.violations.size() == 3);
    assert(result.violations[0].ruleName == "metal1.width");
    assert(result.violations[0].count == 5);
    assert(result.violations[1].count == 3);
    assert(result.violations[2].count == 0);
}

TEST(klayout_drc_empty) {
    QString output = "";
    auto result = KLayoutReportParser::parseDRC(output);
    assert(!result.success);
    assert(!result.errors.isEmpty());
}

// ============================================================================
// KLAYOUT LVS PARSER TESTS
// ============================================================================

TEST(klayout_lvs_match) {
    QString output = R"(
KLayout LVS Report
==================
Layout devices: 8 | Schematic devices: 8
Layout nets: 12 | Schematic nets: 12
Total mismatches: 0
MATCH — Layout matches schematic.
    )";
    auto result = KLayoutReportParser::parseLVS(output);
    assert(result.success);
    assert(result.matched);
    assert(result.deviceCountLayout == 8);
    assert(result.deviceCountSchematic == 8);
    assert(result.netCountLayout == 12);
    assert(result.netCountSchematic == 12);
    assert(result.mismatchCount == 0);
}

TEST(klayout_lvs_mismatch) {
    QString output = R"(
KLayout LVS Report
==================
Layout devices: 9 | Schematic devices: 8
Layout nets: 13 | Schematic nets: 12
Mismatch: device | Layout: NMOS_extra | Schematic: (none) | Extra device in layout
Mismatch: net | Layout: NET_A | Schematic: net_a | Unmatched nets
Total mismatches: 2
MISMATCH — Layout does NOT match schematic.
    )";
    auto result = KLayoutReportParser::parseLVS(output);
    assert(result.success);
    assert(!result.matched);
    assert(result.deviceCountLayout == 9);
    assert(result.deviceCountSchematic == 8);
    assert(result.mismatchCount == 2);
    assert(result.mismatches.size() == 2);
    assert(result.mismatches[0].type == "device");
    assert(result.mismatches[0].layoutName == "NMOS_extra");
    assert(result.mismatches[1].type == "net");
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "\n===== EDA Driver Tests =====\n\n";
    
    // Tests are auto-registered via static initializers above
    
    std::cout << "\n===========================\n";
    std::cout << "Passed: " << testsPassed << "\n";
    std::cout << "Failed: " << testsFailed << "\n";
    
    if (testsFailed > 0) {
        std::cerr << "\n*** " << testsFailed << " TEST(S) FAILED ***\n";
        return 1;
    }
    
    std::cout << "\nAll tests passed.\n";
    return 0;
}
