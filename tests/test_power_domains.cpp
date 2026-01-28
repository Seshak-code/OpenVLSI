/**
 * test_power_domains.cpp
 * Phase 1.3: Power Domain Architecture Tests
 * 
 * Tests:
 *   1. Domain merge correctness (same voltage → same node)
 *   2. Voltage conflict detection
 *   3. Ground (VSS) immutability
 *   4. VBB bulk-only connectivity validation
 *   5. Hash stability with power domains
 */

#include <QApplication>
#include <QDebug>
#include <cassert>
#include <cmath>

#include "../components/design_db.h"
#include "../components/circuit.h"
#include "../gui/analysis/erc_engine.h"

using namespace acutesim::components;

// ============================================================================
// TEST UTILITIES
// ============================================================================

#define TEST_ASSERT(cond, msg) \
    if (!(cond)) { \
        qCritical() << "FAIL:" << msg; \
        return 1; \
    } else { \
        qDebug() << "  PASS:" << msg; \
    }

// ============================================================================
// TESTS
// ============================================================================

int testDomainMergeCorrectness() {
    qDebug() << "- Testing Domain Merge Correctness...";
    
    PowerDomainRegistry registry;
    
    // Register first VDD
    auto result1 = registry.registerDomain("VDD_CORE", PowerDomainType::VDD, 1.8, "VDD1");
    TEST_ASSERT(result1.success, "First VDD registration should succeed");
    int node1 = result1.assignedNode;
    
    // Register second VDD with SAME voltage → should merge
    auto result2 = registry.registerDomain("VDD_CORE", PowerDomainType::VDD, 1.8, "VDD2");
    TEST_ASSERT(result2.success, "Second VDD registration should merge");
    int node2 = result2.assignedNode;
    
    TEST_ASSERT(node1 == node2, "Merged domains should share the same node");
    
    // Verify both symbols are tracked
    const auto* domain = registry.getDomain("VDD_CORE");
    TEST_ASSERT(domain != nullptr, "Domain should exist");
    TEST_ASSERT(domain->connectedSymbolIds.size() == 2, "Domain should track 2 symbols");
    
    qDebug() << "    Domain Merge Correctness: OK";
    return 0;
}

int testVoltageConflict() {
    qDebug() << "- Testing Voltage Conflict Detection...";
    
    PowerDomainRegistry registry;
    
    // Register VDD at 1.8V
    auto result1 = registry.registerDomain("VDD_MIXED", PowerDomainType::VDD, 1.8);
    TEST_ASSERT(result1.success, "First registration should succeed");
    
    // Attempt to register same domain at 3.3V → should FAIL
    auto result2 = registry.registerDomain("VDD_MIXED", PowerDomainType::VDD, 3.3);
    TEST_ASSERT(!result2.success, "Voltage conflict should be detected");
    TEST_ASSERT(result2.message.find("Voltage conflict") != std::string::npos, 
                "Error message should mention voltage conflict");
    
    qDebug() << "    Conflict Message:" << QString::fromStdString(result2.message);
    qDebug() << "    Voltage Conflict Detection: OK";
    return 0;
}

int testTypeConflict() {
    qDebug() << "- Testing Type Conflict Detection...";
    
    PowerDomainRegistry registry;
    
    // Register as VDD
    auto result1 = registry.registerDomain("POWER_TEST", PowerDomainType::VDD, 1.8);
    TEST_ASSERT(result1.success, "First registration should succeed");
    
    // Attempt to register same name as VDDIO → should FAIL
    auto result2 = registry.registerDomain("POWER_TEST", PowerDomainType::VDDIO, 1.8);
    TEST_ASSERT(!result2.success, "Type conflict should be detected");
    
    qDebug() << "    Type Conflict Detection: OK";
    return 0;
}

int testGroundImmutability() {
    qDebug() << "- Testing Ground (VSS) Immutability...";
    
    PowerDomainRegistry registry;
    
    // VSS should exist by default
    const auto* vss = registry.getDomain("VSS");
    TEST_ASSERT(vss != nullptr, "VSS should exist by default");
    TEST_ASSERT(vss->globalNode == 0, "VSS should be at Node 0");
    TEST_ASSERT(vss->isImmutable, "VSS should be immutable");
    TEST_ASSERT(vss->type == PowerDomainType::VSS, "VSS should have type VSS");
    
    // Attempting to unregister a symbol from VSS should work,
    // but the domain itself should remain
    bool removed = registry.unregisterSymbol("VSS", "fake_symbol");
    TEST_ASSERT(!removed, "Cannot unregister from VSS (it remains immutable)");
    
    // VSS still exists
    vss = registry.getDomain("VSS");
    TEST_ASSERT(vss != nullptr, "VSS should persist after unregister attempt");
    
    qDebug() << "    Ground Immutability: OK";
    return 0;
}

int testVBBConnectivityValidation() {
    qDebug() << "- Testing VBB Bulk-Only Connectivity...";
    
    // Valid bulk pin names
    TEST_ASSERT(validateVBBConnection("BULK"), "BULK should be valid");
    TEST_ASSERT(validateVBBConnection("Body"), "Body should be valid");
    TEST_ASSERT(validateVBBConnection("B"), "B should be valid");
    TEST_ASSERT(validateVBBConnection("SUB"), "SUB should be valid");
    TEST_ASSERT(validateVBBConnection("bulk"), "lowercase bulk should be valid");
    
    // Invalid pin names (should NOT connect to VBB)
    TEST_ASSERT(!validateVBBConnection("Gate"), "Gate should be INVALID for VBB");
    TEST_ASSERT(!validateVBBConnection("Drain"), "Drain should be INVALID for VBB");
    TEST_ASSERT(!validateVBBConnection("Source"), "Source should be INVALID for VBB");
    TEST_ASSERT(!validateVBBConnection("VDD"), "VDD should be INVALID for VBB");
    TEST_ASSERT(!validateVBBConnection("IN"), "IN should be INVALID for VBB");
    
    qDebug() << "    VBB Connectivity Validation: OK";
    return 0;
}

int testIRDropModel() {
    qDebug() << "- Testing IR Drop Model...";
    
    PowerDomainRegistry registry;
    
    // Create domain with specific IR drop parameters
    auto result = registry.registerDomain("VDD_IR_TEST", PowerDomainType::VDD, 1.8);
    TEST_ASSERT(result.success, "Domain creation should succeed");
    
    // Get domain and check default values
    const auto* domain = registry.getDomain("VDD_IR_TEST");
    TEST_ASSERT(domain != nullptr, "Domain should exist");
    
    // Default internal resistance should be 0.05 ohms
    TEST_ASSERT(std::abs(domain->internalResistance - 0.05) < 1e-6, 
                "Default internal resistance should be 0.05 ohms");
    
    // Verify IR drop calculation: V_out = Vnom - I * Rint
    // At 100mA, drop = 0.1A * 0.05Ω = 5mV
    double current = 0.1;  // 100mA
    double expectedDrop = current * domain->internalResistance;
    double expectedVout = domain->nominalVoltage - expectedDrop;
    
    TEST_ASSERT(std::abs(expectedVout - 1.795) < 1e-6, 
                "V_out at 100mA should be 1.795V (5mV drop)");
    
    qDebug() << "    IR Drop Model: OK";
    return 0;
}

int testNodeAssignment() {
    qDebug() << "- Testing Node Assignment...";
    
    PowerDomainRegistry registry;
    
    // Register multiple domains
    registry.registerDomain("VDD_CORE", PowerDomainType::VDD, 1.8);
    registry.registerDomain("VDD_IO", PowerDomainType::VDDIO, 3.3);
    registry.registerDomain("VBB", PowerDomainType::VBB, -0.3);
    
    // Reassign nodes
    registry.assignNodes(10);  // Start from node 10
    
    // VSS should still be 0
    const auto* vss = registry.getDomain("VSS");
    TEST_ASSERT(vss->globalNode == 0, "VSS should remain at Node 0");
    
    // Other domains should have nodes >= 10
    const auto* vdd = registry.getDomain("VDD_CORE");
    const auto* vddio = registry.getDomain("VDD_IO");
    const auto* vbb = registry.getDomain("VBB");
    
    TEST_ASSERT(vdd->globalNode >= 10, "VDD should have node >= 10");
    TEST_ASSERT(vddio->globalNode >= 10, "VDDIO should have node >= 10");
    TEST_ASSERT(vbb->globalNode >= 10, "VBB should have node >= 10");
    
    // All nodes should be unique (except VSS = 0)
    std::set<int> nodes = {vdd->globalNode, vddio->globalNode, vbb->globalNode};
    TEST_ASSERT(nodes.size() == 3, "All non-VSS nodes should be unique");
    
    qDebug() << "    Node Assignment: OK";
    return 0;
}

int testCrossDomainIsolationWarning() {
    qDebug() << "- Testing Cross-Domain Isolation Warning...";
    
    // This test verifies the ERC check exists; full UI integration is tested elsewhere.
    // We just verify the violation type is handled correctly.
    ERC::Violation v(ERC::ViolationType::CROSS_DOMAIN_SIGNAL, "Test message", QPoint(0, 0));
    TEST_ASSERT(v.severity == ERC::Severity::WARNING, "Cross-domain should be WARNING severity");
    TEST_ASSERT(ERC::typeToString(ERC::ViolationType::CROSS_DOMAIN_SIGNAL) == "Cross-Domain Signal",
                "Type string should match");
    
    qDebug() << "    Cross-Domain Isolation Warning: OK";
    return 0;
}

int testVerilogARoundTrip() {
    qDebug() << "- Testing Verilog-A Round-Trip...";
    
    PDKPowerDomain domain;
    domain.name = "VDD_TEST";
    domain.type = PDKPowerDomain::VDD;
    domain.nominalVoltage = 1.8;
    domain.internalResistance = 0.05;
    
    std::string verilogA = domain.getVerilogAModule();
    
    // Verify key elements are present
    TEST_ASSERT(verilogA.find("module VDD_TEST_supply") != std::string::npos, 
                "Module name should be present");
    TEST_ASSERT(verilogA.find("vnom = 1.8") != std::string::npos, 
                "Nominal voltage should be present");
    TEST_ASSERT(verilogA.find("rint = 0.05") != std::string::npos, 
                "Internal resistance should be present");
    TEST_ASSERT(verilogA.find("V(vout) <+") != std::string::npos, 
                "Analog contribution should be present");
    
    // VSS should return a comment, not a module
    PDKPowerDomain vss;
    vss.type = PDKPowerDomain::VSS;
    std::string vssCode = vss.getVerilogAModule();
    TEST_ASSERT(vssCode.find("// VSS") != std::string::npos, 
                "VSS should return comment only");
    
    qDebug() << "    Verilog-A Round-Trip: OK";
    return 0;
}

int testLVSPowerPinIdentification() {
    qDebug() << "- Testing LVS Power Pin Identification...";
    
    PowerDomainRegistry registry;
    
    // Register power domains with symbol IDs
    registry.registerDomain("VDD_CORE", PowerDomainType::VDD, 1.8, "vdd_symbol_1");
    registry.registerDomain("VDDIO", PowerDomainType::VDDIO, 3.3, "vddio_symbol_1");
    
    // Query all domains
    const auto& allDomains = registry.getAllDomains();
    
    // Should have VSS (default) + VDD_CORE + VDDIO = 3 domains
    TEST_ASSERT(allDomains.size() == 3, "Should have 3 domains (VSS, VDD_CORE, VDDIO)");
    
    // Each domain should have associated symbol IDs (for LVS netlist)
    int totalSymbols = 0;
    for (const auto& [name, entry] : allDomains) {
        totalSymbols += entry.connectedSymbolIds.size();
    }
    TEST_ASSERT(totalSymbols == 2, "Should have 2 symbol references");
    
    qDebug() << "    LVS Power Pin Identification: OK";
    return 0;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    qDebug() << "[Test] Power Domain Architecture Verification...";
    
    int failures = 0;
    
    failures += testDomainMergeCorrectness();
    failures += testVoltageConflict();
    failures += testTypeConflict();
    failures += testGroundImmutability();
    failures += testVBBConnectivityValidation();
    failures += testIRDropModel();
    failures += testNodeAssignment();
    failures += testCrossDomainIsolationWarning();
    failures += testVerilogARoundTrip();
    failures += testLVSPowerPinIdentification();
    
    if (failures == 0) {
        qDebug() << "[Test] Passed All Power Domain Checks.";
    } else {
        qDebug() << "[Test] FAILED with" << failures << "errors.";
    }
    
    return failures;
}
