#include "acutesim_engine/netlist/circuit.h"
#include "../components/component_library.h"
#include "acutesim_engine/netlist/subcircuit_validator.h"
#include "acutesim_engine/physics/design_rules.h"
#include <iostream>
#include <iomanip>
#include <cassert>

// Include solver
#include "acutesim_engine/solvers/circuitsim_solver.cpp"
#include "acutesim_engine/tensors/physics_tensors.cpp"

/**
 * Automated flow testing for Raw State component integration.
 * 
 * This test validates:
 * 1. Circuits can be extracted as integrated components
 * 2. Raw State ERC rules are enforced during promotion
 * 3. Components can be integrated into larger designs
 * 4. Physics-backed rules catch integration errors
 */

using namespace DesignRules;

// ============================================================================
// TEST HELPERS
// ============================================================================

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void printResult(const std::string& test, bool passed) {
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << test << "\n";
}

// ============================================================================
// TEST 1: BENCH CIRCUIT CREATION & VALIDATION
// ============================================================================

bool testRawStateCircuit() {
    printHeader("Test 1: Raw State Circuit Creation");
    
    // Create a simple voltage divider
    TensorBlock rawCircuit;
    rawCircuit.name = "VoltageDivider_Raw";
    
    // Add RailDriver (required in raw state)
    VoltageSource vs;
    vs.nodePositive = 1;
    vs.nodeNegative = 0;
    vs.voltage_V = 10.0;
    vs.type = "RailDriver";
    rawCircuit.voltageSources.push_back(vs);
    
    // Add resistive divider
    rawCircuit.addResistor(1, 2, 10000.0);  // R1: 10k
    rawCircuit.addResistor(2, 0, 10000.0);  // R2: 10k
    
    // Validate
    ViolationReport report = validate(rawCircuit);
    
    std::cout << "Violations: " << report.entries.size() << "\n";
    for (const auto& e : report.entries) {
        std::cout << "  - " << e.message << "\n";
    }
    
    // Raw state should allow RailDrivers
    bool passed = !report.hasErrors;
    printResult("Raw circuit with RailDriver is valid", passed);
    
    return passed;
}

// ============================================================================
// TEST 2: BENCH → BLOCK PROMOTION
// ============================================================================

bool testBenchToBlockPromotion() {
    printHeader("Test 2: Bench → Block Promotion");
    
    // Create a bench circuit
    TensorNetlist benchNet;
    benchNet.addResistor(1, 2, 5000.0);   // R1
    benchNet.addResistor(2, 0, 5000.0);   // R2
    
    // Extract as block (subcircuit)
    std::vector<PortInfo> ports = {
        {"VIN", 1, false},
        {"VOUT", 2, false},
        {"GND", 0, true}
    };
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        benchNet,
        "ResistiveDivider",
        "10k:10k voltage divider block",
        ports
    );
    
    // Validate the extracted subcircuit
    ValidationResult valResult = validateSubCircuit(blockDef);
    
    std::cout << "Subcircuit extraction: " << valResult.summary() << "\n";
    
    bool extractPassed = valResult.isValid;
    printResult("Extracted subcircuit is valid", extractPassed);
    
    // Now validate the internal block in Raw mode
    ViolationReport blockReport = validate(blockDef.internalBlock);
    
    // Should pass if no non-RailDriver sources
    bool blockPassed = !blockReport.hasErrors;
    printResult("Raw mode validation passed", blockPassed);
    
    return extractPassed && blockPassed;
}

// ============================================================================
// TEST 3: BLOCK MODE REJECTS INTERFACE SOURCES
// ============================================================================

bool testRawStateRejectsGenericSources() {
    printHeader("Test 3: Raw State Rejects Generic Sources");
    
    // Create a block with generic voltage source (should fail)
    TensorBlock badBlock;
    badBlock.name = "BadBlock_WithSource";
    
    // Add generic voltage source (NOT allowed in Raw State)
    VoltageSource vs;
    vs.nodePositive = 1;
    vs.nodeNegative = 0;
    vs.voltage_V = 5.0;
    vs.type = "Generic";
    badBlock.voltageSources.push_back(vs);
    
    badBlock.addResistor(1, 2, 1000.0);
    
    // Validate
    ViolationReport report = validate(badBlock);
    
    std::cout << "Raw mode violations: " << report.entries.size() << "\n";
    for (const auto& e : report.entries) {
        std::cout << "  - [" << (int)e.code << "] " << e.message << "\n";
    }
    
    // Should have INDEPENDENT_SOURCE error
    bool hasSourceError = false;
    for (const auto& e : report.entries) {
        if (e.code & Violation::INDEPENDENT_SOURCE) {
            hasSourceError = true;
            break;
        }
    }
    
    printResult("Raw state correctly rejects generic voltage source", hasSourceError);
    
    return hasSourceError;
}

// ============================================================================
// TEST 4: FLOATING GATE DETECTION
// ============================================================================

bool testFloatingGateDetection() {
    printHeader("Test 4: Floating Gate Detection");
    
    // Create circuit with floating gate MOSFET
    TensorBlock circuit;
    circuit.name = "FloatingGateMOS";
    
    // Add MOSFET with gate disconnected
    Mosfet m;
    m.instanceName = "M1";
    m.drain = 2;
    m.gate = 3;      // Node 3 has no DC path
    m.source = 0;
    m.body = 0;
    m.w = 10e-6;
    m.l = 1e-6;
    circuit.mosfets.push_back(m);
    
    // Add resistor load
    circuit.addResistor(1, 2, 1000.0);
    
    // Validate
    ViolationReport report = validate(circuit);
    
    std::cout << "Floating gate check violations: " << report.entries.size() << "\n";
    for (const auto& e : report.entries) {
        std::cout << "  - [" << (int)e.code << "] " << e.message << "\n";
    }
    
    // Should detect floating gate
    bool hasFloatingGate = false;
    for (const auto& e : report.entries) {
        if (e.code & Violation::FLOATING_GATE) {
            hasFloatingGate = true;
            break;
        }
    }
    
    printResult("Floating gate detected in Block mode", hasFloatingGate);
    
    return hasFloatingGate;
}

// ============================================================================
// TEST 5: BLOCK → SYSTEM INTEGRATION
// ============================================================================

bool testBlockToSystemIntegration() {
    printHeader("Test 5: Block → System Integration");
    
    // Create a valid block (no independent sources)
    TensorBlock validBlock;
    validBlock.name = "ValidAmplifierBlock";
    
    // BJT amplifier stage (no independent sources)
    BJT q;
    q.instanceName = "Q1";
    q.nodeCollector = 2;
    q.base = 1;
    q.emitter = 0;
    q.isNPN = true;
    q.saturationCurrent_I_S_A = 1e-14;
    q.betaF = 100;
    validBlock.bjts.push_back(q);
    
    // Bias resistors
    validBlock.addResistor(3, 1, 100000.0);  // Base bias
    validBlock.addResistor(1, 0, 10000.0);   // Base-emitter
    validBlock.addResistor(4, 2, 5000.0);    // Collector load
    
    // Validate in Raw State
    ViolationReport systemReport = validate(validBlock);
    
    bool blockValid = !systemReport.hasErrors;
    
    std::cout << "Raw mode violations: " << systemReport.entries.size() << "\n";
    for (const auto& e : systemReport.entries) {
        auto severity = e.getSeverity();
        std::string sevStr = (severity == ViolationEntry::Severity::ERR) ? "ERR" :
                             (severity == ViolationEntry::Severity::WARN) ? "WARN" : "INFO";
        std::cout << "  - [" << sevStr << "] " << e.message << "\n";
    }
    
    // Block should be valid
    printResult("Valid block passes raw state validation", blockValid);
    
    return blockValid;
}

// ============================================================================
// TEST 6: COMPLETE FLOW - BENCH → BLOCK → SIMULATE
// ============================================================================

bool testCompleteFlow() {
    printHeader("Test 6: Complete Flow - Bench → Block → Simulate");
    
    // Step 1: Create circuit
    TensorNetlist benchNet;
    benchNet.addVoltageSource(1, 0, 5.0, "RailDriver");
    benchNet.addResistor(1, 2, 2000.0);
    benchNet.addResistor(2, 0, 2000.0);
    
    std::cout << "Step 1: Created bench circuit\n";
    
    // Step 2: Simulate in bench mode
    CircuitSim sim;
    SolverStep benchResult = sim.solveDC(benchNet);
    
    if (!benchResult.stats.converged) {
        printResult("Bench simulation converged", false);
        return false;
    }
    
    double V2_bench = benchResult.nodeVoltages[1];  // Node 2
    std::cout << "Step 2: Bench simulation V(2) = " << V2_bench << " V\n";
    
    // Step 3: Extract as block
    std::vector<PortInfo> ports = {
        {"VIN", 1, false},
        {"VMID", 2, false},
        {"GND", 0, true}
    };
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        benchNet,
        "DividerBlock",
        "2k:2k divider",
        ports
    );
    
    std::cout << "Step 3: Extracted block '" << blockDef.name << "'\n";
    
    // Step 4: Validate block
    ValidationResult valResult = validateSubCircuit(blockDef);
    ViolationReport drReport = validate(blockDef.internalBlock);
    
    std::cout << "Step 4: Block validation - " << valResult.summary() << "\n";
    
    bool blockValid = valResult.isValid;
    
    // Step 5: Report result
    std::cout << "Step 5: Complete flow result\n";
    printResult("Complete Bench → Block flow executed", blockValid);
    
    return blockValid && std::abs(V2_bench - 2.5) < 0.1;
}

// ============================================================================
// TEST 7: ERC AT EACH ABSTRACTION LEVEL
// ============================================================================

bool testERCAtEachLevel() {
    printHeader("Test 7: ERC at Each Abstraction Level");
    
    // Create a circuit that has level-specific violations
    TensorBlock circuit;
    circuit.name = "MultiLevelERCTest";
    
    // Add circuit with potential issues
    circuit.addResistor(1, 2, 1000.0);    // R1
    circuit.addResistor(2, 0, 1000.0);    // R2
    
    // Add a very low resistance (potential short)
    circuit.addResistor(3, 4, 0.5);       // Very low R
    
    // Validate in raw state
    auto report = validate(circuit);
    
    std::cout << "Raw mode:  " << report.entries.size() << " violations, hasErrors=" << report.hasErrors << "\n";
    
    // Should detect VDD/GND short potential
    bool detected = (report.entries.size() > 0);
    
    printResult("ERC detected low-resistance path", detected);
    
    return detected;
}

// ============================================================================
// TEST 8: DIODE RECTIFIER BLOCK (ANALOG)
// ============================================================================

bool testDiodeRectifierBlock() {
    printHeader("Test 8: Diode Rectifier Block (Analog)");
    
    // Create half-wave rectifier circuit
    TensorNetlist rectifier;
    rectifier.addVoltageSource(1, 0, 10.0, "RailDriver"); 
    
    // Add diode (D1)
    Diode d;
    d.anode = 1;
    d.cathode = 2;
    d.saturationCurrent_I_S_A = 1e-12;
    d.emissionCoefficient_N = 1.0;
    d.thermalVoltage_V_T_V = 0.026;
    rectifier.globalBlock.diodes.push_back(d);
    
    // Load resistor
    rectifier.addResistor(2, 0, 1000.0);
    
    // Simulate
    CircuitSim sim;
    SolverStep result = sim.solveDC(rectifier);
    
    if (!result.stats.converged) {
        std::cout << "Rectifier did not converge (res=" << result.stats.residual << ")\n";
        printResult("Diode rectifier converged", false);
        return false;
    }
    
    double Vout = result.nodeVoltages[1];  // Node 2 output
    std::cout << "Rectifier V(out) = " << Vout << " V\n";
    
    // Extract as block
    std::vector<PortInfo> ports = {
        {"VIN", 1, false},
        {"VOUT", 2, false},
        {"GND", 0, true}
    };
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        rectifier, "HalfWaveRectifier", "Diode half-wave rectifier", ports
    );
    
    ValidationResult valResult = validateSubCircuit(blockDef);
    bool valid = valResult.isValid;
    
    std::cout << "Block validation: " << valResult.summary() << "\n";
    printResult("Diode rectifier block extraction", valid);
    
    // Vout should be ~Vin - Vd (9.3-9.7V)
    return valid && result.stats.converged;
}

// ============================================================================
// TEST 9: CMOS INVERTER BLOCK (DIGITAL)
// ============================================================================

bool testCMOSInverterBlock() {
    printHeader("Test 9: CMOS Inverter Block (Digital)");
    
    // Create CMOS inverter with NMOS and PMOS
    TensorBlock inverter;
    inverter.name = "CMOSInverter";
    
    // NMOS (M1): pulls output to GND when input is high
    Mosfet nmos;
    nmos.instanceName = "M1_NMOS";
    nmos.drain = 2;      // Output
    nmos.gate = 1;       // Input
    nmos.source = 0;     // GND
    nmos.body = 0;
    nmos.w = 2e-6;
    nmos.l = 0.18e-6;
    inverter.mosfets.push_back(nmos);
    
    // PMOS (M2): pulls output to VDD when input is low
    Mosfet pmos;
    pmos.instanceName = "M2_PMOS";
    pmos.drain = 2;      // Output
    pmos.gate = 1;       // Input
    pmos.source = 3;     // VDD (node 3)
    pmos.body = 3;
    pmos.w = 4e-6;  // 2x NMOS for symmetric drive
    pmos.l = 0.18e-6;
    inverter.mosfets.push_back(pmos);
    
    // Bias resistors for gates (ensure DC path)
    inverter.addResistor(1, 0, 100000.0);  // Input bias to GND
    inverter.addResistor(3, 0, 100000.0);  // VDD rail bias
    
    // Validate
    ViolationReport report = validate(inverter);
    
    std::cout << "CMOS Inverter violations: " << report.entries.size() << "\n";
    for (const auto& e : report.entries) {
        std::cout << "  - " << e.message << "\n";
    }
    
    // Extract as subcircuit
    std::vector<PortInfo> ports = {
        {"VIN", 1, false},
        {"VOUT", 2, false},
        {"VDD", 3, true},
        {"GND", 0, true}
    };
    
    TensorNetlist netWrapper;
    netWrapper.globalBlock = inverter;
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        netWrapper, "CMOS_Inverter", "Standard CMOS inverter cell", ports
    );
    
    ValidationResult valResult = validateSubCircuit(blockDef);
    bool valid = valResult.isValid;
    
    std::cout << "Block validation: " << valResult.summary() << "\n";
    printResult("CMOS inverter block passes validation", valid);
    
    return valid;
}

// ============================================================================
// TEST 10: BJT DIFFERENTIAL PAIR (ANALOG)
// ============================================================================

bool testBJTDifferentialPair() {
    printHeader("Test 10: BJT Differential Pair (Analog)");
    
    // Classic differential amplifier with tail current source emulated by resistor
    TensorBlock diffPair;
    diffPair.name = "BJT_DiffPair";
    
    // Q1: Left transistor
    BJT q1;
    q1.instanceName = "Q1";
    q1.nodeCollector = 3;      // Out-
    q1.base = 1;               // In+
    q1.emitter = 5;            // Common tail
    q1.isNPN = true;
    q1.saturationCurrent_I_S_A = 1e-14;
    q1.betaF = 100;
    diffPair.bjts.push_back(q1);
    
    // Q2: Right transistor
    BJT q2;
    q2.instanceName = "Q2";
    q2.nodeCollector = 4;      // Out+
    q2.base = 2;               // In-
    q2.emitter = 5;            // Common tail
    q2.isNPN = true;
    q2.saturationCurrent_I_S_A = 1e-14;
    q2.betaF = 100;
    diffPair.bjts.push_back(q2);
    
    // Collector load resistors
    diffPair.addResistor(6, 3, 10000.0);   // RC1 (VCC to Out-)
    diffPair.addResistor(6, 4, 10000.0);   // RC2 (VCC to Out+)
    
    // Tail resistor (emulates current source)
    diffPair.addResistor(5, 0, 5000.0);    // RE (Tail to GND)
    
    // Input bias resistors
    diffPair.addResistor(1, 0, 50000.0);   // RB1
    diffPair.addResistor(2, 0, 50000.0);   // RB2
    
    // VCC bias
    diffPair.addResistor(6, 0, 100000.0);  // VCC path
    
    // Validate
    ViolationReport report = validate(diffPair);
    
    std::cout << "Diff pair violations: " << report.entries.size() << "\n";
    for (const auto& e : report.entries) {
        std::cout << "  - " << e.message << "\n";
    }
    
    // Extract as block
    std::vector<PortInfo> ports = {
        {"IN_P", 1, false},    // In+
        {"IN_N", 2, false},    // In-
        {"OUT_P", 4, false},   // Out+
        {"OUT_N", 3, false},   // Out-
        {"VCC", 6, true},
        {"GND", 0, true}
    };
    
    TensorNetlist netWrapper;
    netWrapper.globalBlock = diffPair;
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        netWrapper, "DifferentialPair", "NPN differential amplifier pair", ports
    );
    
    ValidationResult valResult = validateSubCircuit(blockDef);
    std::cout << "Block validation: " << valResult.summary() << "\n";
    
    bool passed = valResult.isValid;
    printResult("BJT differential pair block valid", passed);
    
    return passed;
}

// ============================================================================
// TEST 11: RC FILTER (MIXED SIGNAL - TRANSIENT CAPABLE)
// ============================================================================

bool testRCFilterBlock() {
    printHeader("Test 11: RC Low-Pass Filter (Mixed-Signal)");
    
    // Create simple RC low-pass filter
    TensorBlock rcFilter;
    rcFilter.name = "RC_LowPass";
    
    // Series resistor
    rcFilter.addResistor(1, 2, 10000.0);   // 10k
    
    // Shunt capacitor
    Capacitor c;
    c.nodePlate1 = 2;
    c.nodePlate2 = 0;
    c.capacitance_farads = 1e-9;  // 1nF
    rcFilter.capacitors.push_back(c);
    
    // Cutoff frequency: fc = 1/(2π*R*C) = 15.9 kHz
    double R = 10000.0;
    double C_val = 1e-9;
    double fc = 1.0 / (2.0 * 3.14159 * R * C_val);
    std::cout << "Calculated cutoff frequency: " << fc / 1000.0 << " kHz\n";
    
    // Validate
    ViolationReport report = validate(rcFilter);
    std::cout << "RC filter violations: " << report.entries.size() << "\n";
    
    // Extract as block
    std::vector<PortInfo> ports = {
        {"VIN", 1, false},
        {"VOUT", 2, false},
        {"GND", 0, true}
    };
    
    TensorNetlist netWrapper;
    netWrapper.globalBlock = rcFilter;
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        netWrapper, "RC_LowPass_1nF", "First-order RC low-pass filter, fc=15.9kHz", ports
    );
    
    ValidationResult valResult = validateSubCircuit(blockDef);
    std::cout << "Block validation: " << valResult.summary() << "\n";
    
    bool passed = valResult.isValid && (blockDef.internalBlock.capacitors.size() == 1);
    printResult("RC filter with capacitor extracted correctly", passed);
    
    return passed;
}

// ============================================================================
// TEST 12: FULL ANALOG BLOCK INTEGRATION
// ============================================================================

bool testFullAnalogIntegration() {
    printHeader("Test 12: Full Analog Block Integration");
    
    // Create a complete analog signal chain:
    // Input buffer → Amplifier stage → Output buffer
    
    TensorNetlist analogChain;
    
    // Input stage: Emitter follower (buffer)
    BJT q_in;
    q_in.instanceName = "Q_IN";
    q_in.nodeCollector = 4;    // VCC
    q_in.base = 1;             // Input
    q_in.emitter = 2;          // Buffer output
    q_in.isNPN = true;
    q_in.saturationCurrent_I_S_A = 1e-14;
    q_in.betaF = 100;
    analogChain.globalBlock.bjts.push_back(q_in);
    
    // Gain stage: Common emitter
    BJT q_amp;
    q_amp.instanceName = "Q_AMP";
    q_amp.nodeCollector = 3;   // Amplified output
    q_amp.base = 2;            // From buffer
    q_amp.emitter = 0;         // GND
    q_amp.isNPN = true;
    q_amp.saturationCurrent_I_S_A = 1e-14;
    q_amp.betaF = 100;
    analogChain.globalBlock.bjts.push_back(q_amp);
    
    // Bias network
    analogChain.addResistor(4, 1, 100000.0);   // Input bias from VCC
    analogChain.addResistor(1, 0, 100000.0);   // Input bias to GND
    analogChain.addResistor(2, 0, 1000.0);     // Emitter follower load
    analogChain.addResistor(4, 3, 5000.0);     // Collector load
    analogChain.addResistor(4, 0, 100000.0);   // VCC reference
    
    // Add coupling capacitor (AC coupling)
    Capacitor c_coupling;
    c_coupling.nodePlate1 = 3;
    c_coupling.nodePlate2 = 5;  // AC output
    c_coupling.capacitance_farads = 10e-6;  // 10µF
    analogChain.globalBlock.capacitors.push_back(c_coupling);
    
    // Output load
    analogChain.addResistor(5, 0, 10000.0);
    
    // Validate entire chain
    ViolationReport report = validate(analogChain.globalBlock);
    std::cout << "Analog chain violations: " << report.entries.size() << "\n";
    for (const auto& e : report.entries) {
        std::cout << "  - " << e.message << "\n";
    }
    
    // Extract as integrated block
    std::vector<PortInfo> ports = {
        {"VIN", 1, false},
        {"VOUT_AC", 5, false},
        {"VCC", 4, true},
        {"GND", 0, true}
    };
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        analogChain, "AnalogAmplifier", "Two-stage BJT amplifier with AC coupling", ports
    );
    
    ValidationResult valResult = validateSubCircuit(blockDef);
    std::cout << "Full analog block validation: " << valResult.summary() << "\n";
    
    // Verify component counts
    bool hasBJTs = blockDef.internalBlock.bjts.size() == 2;
    bool hasCap = blockDef.internalBlock.capacitors.size() == 1;
    bool hasResistors = blockDef.internalBlock.resistors.size() >= 5;
    
    std::cout << "  BJTs: " << blockDef.internalBlock.bjts.size() << "/2\n";
    std::cout << "  Capacitors: " << blockDef.internalBlock.capacitors.size() << "/1\n";
    std::cout << "  Resistors: " << blockDef.internalBlock.resistors.size() << "/6\n";
    
    bool passed = valResult.isValid && hasBJTs && hasCap && hasResistors;
    printResult("Full analog integration block valid", passed);
    
    return passed;
}

// ============================================================================
// TEST 13: MIXED BJT + MOSFET CIRCUIT
// ============================================================================

bool testMixedBJTMOSFET() {
    printHeader("Test 13: Mixed BJT + MOSFET Circuit");
    
    // BiCMOS-style circuit with both BJT and MOSFET
    TensorBlock bicmos;
    bicmos.name = "BiCMOS_Buffer";
    
    // NMOS input stage
    Mosfet m1;
    m1.instanceName = "M1_NMOS";
    m1.drain = 2;
    m1.gate = 1;
    m1.source = 0;
    m1.body = 0;
    m1.w = 10e-6;
    m1.l = 0.5e-6;
    bicmos.mosfets.push_back(m1);
    
    // BJT output stage
    BJT q1;
    q1.instanceName = "Q1_OUTPUT";
    q1.nodeCollector = 3;
    q1.base = 2;       // Driven by MOSFET drain
    q1.emitter = 0;
    q1.isNPN = true;
    q1.saturationCurrent_I_S_A = 1e-13;
    q1.betaF = 150;
    bicmos.bjts.push_back(q1);
    
    // Bias resistors
    bicmos.addResistor(1, 0, 100000.0);   // Gate bias
    bicmos.addResistor(4, 2, 10000.0);    // NMOS load
    bicmos.addResistor(4, 3, 5000.0);     // Collector load
    bicmos.addResistor(4, 0, 100000.0);   // VDD bias
    
    // Validate
    ViolationReport report = validate(bicmos);
    std::cout << "BiCMOS violations: " << report.entries.size() << "\n";
    
    // Extract as block
    std::vector<PortInfo> ports = {
        {"VIN", 1, false},
        {"VOUT", 3, false},
        {"VDD", 4, true},
        {"GND", 0, true}
    };
    
    TensorNetlist netWrapper;
    netWrapper.globalBlock = bicmos;
    
    SubCircuitDefinition blockDef = extractSubCircuit(
        netWrapper, "BiCMOS_Buffer", "MOSFET input + BJT output buffer", ports
    );
    
    ValidationResult valResult = validateSubCircuit(blockDef);
    std::cout << "BiCMOS block validation: " << valResult.summary() << "\n";
    
    bool hasMOS = blockDef.internalBlock.mosfets.size() == 1;
    bool hasBJT = blockDef.internalBlock.bjts.size() == 1;
    
    bool passed = valResult.isValid && hasMOS && hasBJT;
    printResult("Mixed BJT+MOSFET circuit valid", passed);
    
    return passed;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FLOW TESTING: Analog & Mixed-Signal Components          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    int passed = 0;
    int failed = 0;
    
    // Basic flow tests (1-7)
    if (testRawStateCircuit())                 passed++; else failed++;
    if (testBenchToBlockPromotion())           passed++; else failed++;
    if (testRawStateRejectsGenericSources())   passed++; else failed++;
    if (testFloatingGateDetection())           passed++; else failed++;
    if (testBlockToSystemIntegration())        passed++; else failed++;
    if (testCompleteFlow())                    passed++; else failed++;
    if (testERCAtEachLevel())                  passed++; else failed++;
    
    // Analog & Mixed-Signal tests (8-13)
    if (testDiodeRectifierBlock())             passed++; else failed++;
    if (testCMOSInverterBlock())               passed++; else failed++;
    if (testBJTDifferentialPair())             passed++; else failed++;
    if (testRCFilterBlock())                   passed++; else failed++;
    if (testFullAnalogIntegration())           passed++; else failed++;
    if (testMixedBJTMOSFET())                  passed++; else failed++;
    
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  RESULTS: " << std::setw(2) << passed << " passed, " 
              << std::setw(2) << failed << " failed" 
              << std::string(28, ' ') << "║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    return (failed > 0) ? 1 : 0;
}
