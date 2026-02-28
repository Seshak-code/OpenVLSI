#include "../ai_api/introspection_api.h"
#include "acutesim_engine/netlist/circuit.h"
#include <iostream>

/**
 * test_introspection.cpp
 * 
 * Tests for the Introspection API including:
 * - Circuit state serialization
 * - DRC/LVS rule registry
 * - PDK rule overrides
 * - JSON output for AI API
 */

using namespace Introspection;

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

// ============================================================================
// TEST 1: BASIC CIRCUIT STATE
// ============================================================================

bool testCircuitState() {
    printHeader("Test 1: Basic Circuit State");
    
    TensorNetlist net;
    net.globalBlock.name = "TestCircuit";
    net.addResistor(1, 2, 1000.0);
    net.addResistor(2, 0, 1000.0);
    net.addVoltageSource(1, 0, 5.0);
    
    // Add a BJT
    BJT q;
    q.instanceName = "Q1";
    q.nodeCollector = 3;
    q.base = 2;
    q.emitter = 0;
    q.isNPN = true;
    q.saturationCurrent_I_S_A = 1e-14;
    q.betaF = 100;
    net.globalBlock.bjts.push_back(q);
    
    // Get circuit state
    CircuitState state = getCircuitState(net, {});
    
    std::cout << "Circuit: " << state.name << "\n";
    std::cout << "Nodes: " << state.numNodes << "\n";
    std::cout << "Resistors: " << state.numResistors << "\n";
    std::cout << "BJTs: " << state.numBJTs << "\n";
    std::cout << "DRC Errors: " << state.drcErrors << "\n";
    
    bool passed = (state.numResistors == 2 && state.numBJTs == 1);
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " Circuit state captured correctly\n";
    
    return passed;
}

// ============================================================================
// TEST 2: JSON SERIALIZATION
// ============================================================================

bool testJSONSerialization() {
    printHeader("Test 2: JSON Serialization");
    
    TensorNetlist net;
    net.globalBlock.name = "JSONTest";
    net.addResistor(1, 0, 1000.0);
    
    std::vector<double> voltages = {5.0, 2.5};
    CircuitState state = getCircuitState(net, voltages);
    
    std::string json = state.toJSON();
    
    std::cout << "JSON Output:\n" << json.substr(0, 400) << "...\n";
    
    // Basic validation
    bool hasName = json.find("\"name\"") != std::string::npos;
    bool hasPrecision = json.find("\"precision\": \"emulated_f64\"") != std::string::npos;
    bool hasVoltages = json.find("\"nodeVoltages\"") != std::string::npos;
    
    bool passed = hasName && hasPrecision && hasVoltages;
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " JSON contains required fields\n";
    
    return passed;
}

// ============================================================================
// TEST 3: RULE REGISTRY
// ============================================================================

bool testRuleRegistry() {
    printHeader("Test 3: Rule Registry");
    
    auto& registry = RuleRegistry::instance();
    const auto& rules = registry.getAllRules();
    
    std::cout << "Total rules registered: " << rules.size() << "\n";
    
    // Check for key rules
    bool hasFloatingGate = rules.count("ERC.FLOATING_GATE") > 0;
    bool hasMinWidth = rules.count("DRC.MIN_WIDTH.METAL1") > 0;
    bool hasLVS = rules.count("LVS.DEVICE_MISMATCH") > 0;
    
    std::cout << "ERC.FLOATING_GATE: " << (hasFloatingGate ? "found" : "missing") << "\n";
    std::cout << "DRC.MIN_WIDTH.METAL1: " << (hasMinWidth ? "found" : "missing") << "\n";
    std::cout << "LVS.DEVICE_MISMATCH: " << (hasLVS ? "found" : "missing") << "\n";
    
    bool passed = hasFloatingGate && hasMinWidth && hasLVS;
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " Rule registry populated\n";
    
    return passed;
}

// ============================================================================
// TEST 4: PDK OVERRIDE
// ============================================================================

bool testPDKOverride() {
    printHeader("Test 4: PDK Override");
    
    auto& registry = RuleRegistry::instance();
    
    // Get original value
    double originalValue = registry.getAllRules().at("DRC.MIN_WIDTH.METAL1").value;
    std::cout << "Original MIN_WIDTH.METAL1: " << originalValue << " um\n";
    
    // Load SKY130 PDK
    loadPDKRules("sky130");
    
    // Check override
    const auto& rule = registry.getAllRules().at("DRC.MIN_WIDTH.METAL1");
    std::cout << "After SKY130 load: " << rule.value << " um (source: " << rule.pdkSource << ")\n";
    
    bool passed = (rule.pdkSource == "sky130" && rule.isOverridden);
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " PDK override applied correctly\n";
    
    return passed;
}

// ============================================================================
// TEST 5: RULES JSON FOR AI
// ============================================================================

bool testRulesJSON() {
    printHeader("Test 5: Rules JSON for AI");
    
    std::string json = getRulesJSON();
    
    std::cout << "Rules JSON length: " << json.size() << " bytes\n";
    std::cout << "Sample:\n" << json.substr(0, 500) << "...\n";
    
    // Validate structure
    bool hasRulesArray = json.find("\"rules\"") != std::string::npos;
    bool hasCategory = json.find("\"category\"") != std::string::npos;
    bool hasAIHint = json.find("\"aiHint\"") != std::string::npos;
    
    bool passed = hasRulesArray && hasCategory && hasAIHint;
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " Rules JSON has correct structure\n";
    
    return passed;
}

// ============================================================================
// TEST 6: COMPLETE INTROSPECTION FLOW
// ============================================================================

bool testCompleteFlow() {
    printHeader("Test 6: Complete Introspection Flow");
    
    // Create circuit with known issues
    TensorNetlist net;
    net.globalBlock.name = "IntrospectionTest";
    
    // Add MOSFET with potential floating gate
    Mosfet m;
    m.instanceName = "M1";
    m.drain = 2;
    m.gate = 1;
    m.source = 0;
    m.body = 0;
    m.w = 10e-6;
    m.l = 0.18e-6;
    net.globalBlock.mosfets.push_back(m);
    
    // Get state
    std::string json = getCircuitStateJSON(net, {});
    
    std::cout << "Complete state JSON:\n" << json << "\n";
    
    // Should have DRC violations (floating gate)
    bool hasDRC = json.find("\"drc\"") != std::string::npos;
    bool passed = hasDRC;
    
    std::cout << (passed ? "[PASS]" : "[FAIL]") << " Complete introspection flow works\n";
    
    return passed;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  INTROSPECTION API TEST SUITE                            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    int passed = 0;
    int failed = 0;
    
    if (testCircuitState())    passed++; else failed++;
    if (testJSONSerialization()) passed++; else failed++;
    if (testRuleRegistry())    passed++; else failed++;
    if (testPDKOverride())     passed++; else failed++;
    if (testRulesJSON())       passed++; else failed++;
    if (testCompleteFlow())    passed++; else failed++;
    
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  RESULTS: " << passed << " passed, " << failed << " failed";
    std::cout << std::string(35 - std::to_string(passed).length() - std::to_string(failed).length(), ' ') << "║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    return (failed > 0) ? 1 : 0;
}
