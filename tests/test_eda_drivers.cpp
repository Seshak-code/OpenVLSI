/**
 * test_eda_drivers.cpp — Unit tests for EDA tool drivers
 *
 * Tests Yosys log parsing, OpenROAD report parsing, DEF coordinate translation,
 * and KLayout DRC/LVS report parsing. (The flow itself is test_rtl_flow.)
 *
 * All tests are pure unit tests using hardcoded golden strings.
 * NO external tools (Yosys, OpenROAD) are required.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <algorithm>

#include "compute/orchestration/drivers/yosys_log_parser.h"
#include "compute/orchestration/drivers/openroad_report_parser.h"
#include "compute/orchestration/drivers/klayout_report_parser.h"

using namespace acutesim::compute::orchestration;

// ============================================================================
// HELPERS
// ============================================================================

#include "compute/orchestration/drivers/eda_types.h"
using acutesim::compute::orchestration::Point2D;
using acutesim::compute::orchestration::Size2D;

#define ASSERT_CONTAINS(haystack, needle) \
    do { \
        if ((haystack).find(needle) == std::string::npos) { \
            std::cerr << "FAIL: Expected to find \"" << (needle) \
                      << "\" in output.\n" \
                      << "Output was:\n" << (haystack) << "\n"; \
            assert(false && "ASSERT_CONTAINS failed"); \
        } \
    } while (0)

#define ASSERT_NOT_CONTAINS(haystack, needle) \
    do { \
        if ((haystack).find(needle) != std::string::npos) { \
            std::cerr << "FAIL: Did NOT expect \"" << (needle) \
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
// YOSYS LOG PARSER TESTS
// ============================================================================

TEST(yosys_parse_single_module) {
    std::string golden = R"(
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
    assert(result.modules.count("inverter") > 0);
    const auto& mod = result.modules.at("inverter");
    assert(mod.moduleName == "inverter");
    assert(mod.cellCount == 2);
    assert(mod.wireCount == 3);
    assert(mod.wireBits == 3);
    assert(mod.publicWires == 2);
    assert(mod.cellBreakdown.count("$_NOT_") > 0);
    assert(mod.cellBreakdown.at("$_NOT_") == 1);
    assert(mod.cellBreakdown.count("sky130_fd_sc_hd__inv_1") > 0);
    assert(mod.cellBreakdown.at("sky130_fd_sc_hd__inv_1") == 1);
}

TEST(yosys_parse_multi_module) {
    std::string golden = R"(
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
    assert(result.modules.count("adder") > 0);
    assert(result.modules.count("counter") > 0);
    assert(result.modules["adder"].cellCount == 5);
    assert(result.modules["counter"].cellCount == 12);
}

TEST(yosys_parse_with_errors) {
    std::string golden = R"(
ERROR: Module 'missing_module' not found!
Warning: Unused wire 'x' in module 'top'.
)";
    
    SynthesisResult result = YosysLogParser::parse(golden);
    
    assert(!result.success);
    assert(result.errors.size() >= 1);
    assert(result.warnings.size() >= 1);
    assert(result.errors[0].find("missing_module") != std::string::npos);
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
// OPENROAD REPORT PARSER TESTS
// ============================================================================

TEST(openroad_parse_design_area) {
    std::string golden = "Design area 12345.678 u^2 67% utilization.\n";
    auto result = OpenRoadReportParser::parseDesignArea(golden);
    
    ASSERT_NEAR(result.totalArea, 12345.678, 0.01);
    ASSERT_NEAR(result.utilization, 0.67, 0.01);
}

TEST(openroad_parse_timing_clean) {
    std::string golden = R"(
worst slack 0.500
tns 0.000
)";
    auto result = OpenRoadReportParser::parseTiming(golden);
    
    ASSERT_NEAR(result.wns, 0.5, 0.001);
    ASSERT_NEAR(result.tns, 0.0, 0.001);
    assert(result.timingClean);
}

TEST(openroad_parse_timing_violation) {
    std::string golden = R"(
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
    std::string golden = R"(
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
    std::string golden = R"(
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
    std::string golden = R"(
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
    std::string golden = R"(
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
    std::string golden = "[ERROR] Cannot read LEF file: missing.lef\n";
    PnRResult result;
    OpenRoadReportParser::parseErrors(golden, result);
    
    assert(result.errors.size() == 1);
    assert(result.errors[0].find("missing.lef") != std::string::npos);
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
    
    std::vector<SchematicPlacement> placements;
    SchematicPlacement p;
    p.instanceName = "U1";
    p.cellName = "sky130_fd_sc_hd__inv_1";
    p.canvasPosition = Point2D{100.0, 200.0};
    p.rotation = 0.0;
    p.mirrored = false;
    placements.push_back(p);
    
    std::string def = DEFTranslator::generateDEF(
        "test_design", placements,
        Size2D{100.0, 100.0},  // 100x100 um die
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
    std::vector<SchematicPlacement> placements;
    
    SchematicPlacement p1;
    p1.instanceName = "U1";
    p1.cellName = "sky130_fd_sc_hd__inv_1";
    p1.canvasPosition = Point2D{0, 0};
    placements.push_back(p1);
    
    SchematicPlacement p2;
    p2.instanceName = "U2";
    p2.cellName = "sky130_fd_sc_hd__nand2_1";
    p2.canvasPosition = Point2D{500, 500};
    p2.rotation = 90.0;
    placements.push_back(p2);
    
    SchematicPlacement p3;
    p3.instanceName = "U3";
    p3.cellName = "sky130_fd_sc_hd__buf_2";
    p3.canvasPosition = Point2D{250, 750};
    p3.mirrored = true;
    placements.push_back(p3);
    
    std::string def = DEFTranslator::generateDEF(
        "multi_test", placements,
        Size2D{200.0, 200.0}, 0.1, 1000.0, 1000);
    
    ASSERT_CONTAINS(def, "COMPONENTS 3");
    ASSERT_CONTAINS(def, "U1 sky130_fd_sc_hd__inv_1");
    ASSERT_CONTAINS(def, "U2 sky130_fd_sc_hd__nand2_1");
    ASSERT_CONTAINS(def, "U3 sky130_fd_sc_hd__buf_2");
    ASSERT_CONTAINS(def, ") E ;");   // U2: 90°
    ASSERT_CONTAINS(def, ") FN ;");  // U3: mirrored
}

TEST(def_empty_placements) {
    std::vector<SchematicPlacement> empty;
    std::string def = DEFTranslator::generateDEF("empty_design", empty,
                                              Size2D{10, 10});
    ASSERT_CONTAINS(def, "COMPONENTS 0");
    ASSERT_CONTAINS(def, "END DESIGN");
}

TEST(def_header_format) {
    std::vector<SchematicPlacement> placements;
    std::string def = DEFTranslator::generateDEF("mydesign", placements,
                                              Size2D{50, 50});
    ASSERT_CONTAINS(def, "VERSION 5.8");
    ASSERT_CONTAINS(def, "DIVIDERCHAR \"/\"");
    ASSERT_CONTAINS(def, "BUSBITCHARS \"[]\"");
    ASSERT_CONTAINS(def, "UNITS DISTANCE MICRONS 1000");
}

// ============================================================================
// KLAYOUT DRC PARSER TESTS
// ============================================================================

TEST(klayout_drc_clean) {
    std::string output = R"(
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
    std::string output = R"(
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
    std::string output = "";
    auto result = KLayoutReportParser::parseDRC(output);
    assert(!result.success);
    assert(!result.errors.empty());
}

// ============================================================================
// KLAYOUT LVS PARSER TESTS
// ============================================================================

TEST(klayout_lvs_match) {
    std::string output = R"(
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
    std::string output = R"(
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
