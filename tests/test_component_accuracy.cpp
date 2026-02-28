#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <iomanip>

#include "acutesim_engine/netlist/circuit.h"
#include "acutesim_engine/physics/circuitsim.h"
#include "components/standard_cells.h"
#include "acutesim_engine/infrastructure/netlist_compiler.h"

#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

/**
 * test_component_accuracy.cpp
 * 
 * Verifies accuracy of all basic components and standard cells.
 */

void verify_pass(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "[PASS] " << std::setw(40) << std::left << testName << GREEN << "OK" << RESET << "\n";
    } else {
        std::cout << "[FAIL] " << std::setw(40) << std::left << testName << RED << "FAILED" << RESET << "\n";
        exit(1);
    }
}

void test_basic_passives() {
    TensorNetlist nl;
    // V1 (10V) -> R1 (1k) -> Node 1 -> R2 (1k) -> GND
    nl.addVoltageSource(2, 0, 10.0);
    nl.addResistor("R1", 2, 1, 1000.0);
    nl.addResistor("R2", 1, 0, 1000.0);
    
    CircuitSim sim;
    auto step = sim.solveDC(nl);
    
    verify_pass(std::abs(step.nodeVoltages[1] - 5.0) < 1e-6, "Resistive Divider (5.0V)");
}

void test_diode() {
    TensorNetlist nl;
    // V1 (0.7V) -> D1 -> GND
    // I = Is * (exp(V/Vt) - 1)
    nl.addVoltageSource(1, 0, 0.7);
    nl.addDiode(1, 0, 1e-14); 
    
    CircuitSim sim;
    auto step = sim.solveDC(nl);
    
    // Check if converged
    verify_pass(step.stats.converged, "Diode Convergence");
    
    // We can't easily measure current without a resistor or checking internal state.
    // Let's add a series resistor to measure current.
    // V1 (10V) -> R (1k) -> D -> GND
    TensorNetlist nl2;
    nl2.addVoltageSource(2, 0, 10.0);
    nl2.addResistor(2, 1, 1000.0);
    nl2.addDiode(1, 0, 1e-14);
    
    step = sim.solveDC(nl2);
    double vd = step.nodeVoltages[1];
    if (vd > 0.6 && vd < 0.8) {
        std::cout << "[PASS] " << std::setw(40) << std::left << "Diode Drop (~0.7V)" << GREEN << "OK (" << vd << "V)" << RESET << "\n";
    } else {
        std::cout << "[FAIL] " << std::setw(40) << std::left << "Diode Drop (~0.7V)" << RED << "FAILED (" << vd << "V)" << RESET << "\n";
        exit(1);
    }
}

void test_jfet() {
    TensorNetlist nl;
    // N-JFET Common Source
    // VDD (15V) -> RD (2k) -> D
    // G -> GND (0V)
    // S -> GND
    
    // Vgs = 0. Vds should pull up to VDD if Cutoff, or drop if conducting.
    // Vgs=0 > Vto (-2V) -> ON (Idss).
    
    nl.addVoltageSource(2, 0, 15.0); // VDD (Node 2)
    nl.addResistor("RD", 2, 1, 2000.0); // Drain Load (2->1)
    // J1: D=1, G=0, S=0. N-ch. Beta=1e-4, Vto=-2
    nl.addJFET("J1", 1, 0, 0, "default", true, 1e-4, -2.0, 0.0);
    
    CircuitSim sim;
    auto step = sim.solveDC(nl);
    
    double vds = step.nodeVoltages[1];
    double id = (15.0 - vds) / 2000.0;
    
    // Idss = Beta * Vto^2 = 1e-4 * 4 = 400 uA
    // Vds should be 15 - (400uA * 2k) = 15 - 0.8 = 14.2V
    // Wait, saturation?
    // Vds = 14.2V. Vgs - Vto = 0 - (-2) = 2V.
    // Vds > Vgs-Vto (14.2 > 2). Saturation.
    // Id = Beta * (Vgs-Vto)^2 = 400uA.
    
    verify_pass(std::abs(id - 400e-6) < 10e-6, "JFET Idss (Vgs=0)");
}

void test_zener() {
    TensorNetlist nl;
    // V1 (10V) -> R (1k) -> Zener (5.1V) -> GND
    nl.addVoltageSource(2, 0, 10.0);
    nl.addResistor(2, 1, 100.0); // 100 ohm
    // Zener: Anode=0, Cathode=1. (Reverse Biased)
    nl.addZenerDiode("D1", 0, 1, 5.1, 5.0);
    
    CircuitSim sim;
    auto step = sim.solveDC(nl);
    
    double vz = step.nodeVoltages[1];
    if (std::abs(vz - 5.1) < 0.2) {
        std::cout << "[PASS] " << std::setw(40) << std::left << "Zener Regulation (5.1V)" << GREEN << "OK (" << vz << "V)" << RESET << "\n";
    } else {
        std::cout << "[FAIL] " << std::setw(40) << std::left << "Zener Regulation (5.1V)" << RED << "FAILED (" << vz << "V)" << RESET << "\n";
        exit(1);
    }
}

void test_logic_gates() {
    using namespace StandardCells;
    
    // AND2 Truth Table
    // A=0, B=0 -> Y=0
    // A=1, B=0 -> Y=0
    // A=0, B=1 -> Y=0
    // A=1, B=1 -> Y=1
    
    std::cout << "Testing Logic Gates...\n";
    
    auto check_logic = [&](const std::string& type, bool a, bool b, bool expected) {
        TensorNetlist nl;
        const SubCircuitDefinition* def = StandardCellLibrary::instance().get(type);
        if(!def) { std::cout << "FAIL: " << type << " missing\n"; return; }
        
        nl.blockDefinitions[type] = def->internalBlock;
        // Inst: Local nodes 0..4 mapped to Global 0..4
        // Local 0 (VSS) -> Global 0 (GND)
        // Local 1 (A)   -> Global 1 (A)
        // Local 2 (B)   -> Global 2 (B)
        // Local 3 (Y)   -> Global 3 (Y)
        // Local 4 (VDD) -> Global 4 (VDD)
        nl.addInstance(type, {0, 1, 2, 3, 4});
        
        // Power
        nl.addVoltageSource(4, 0, 1.8);
        // Inputs
        nl.addVoltageSource(1, 0, a ? 1.8 : 0.0);
        nl.addVoltageSource(2, 0, b ? 1.8 : 0.0);
        // Load
        nl.addResistor(3, 0, 1e6); // 1M load
        
        CircuitSim sim;
        auto step = sim.solveDC(nl);
        
        double vy = step.nodeVoltages[3];
        bool result = (vy > 0.9);
        
        std::cout << "  " << type << "(" << a << "," << b << ") -> " << vy << "V (" << result << ")\n";
        verify_pass(result == expected, type + " Truth Check");
    };
    
    check_logic("PDK_AND2", false, false, false);
    check_logic("PDK_AND2", true, true, true);
    
    check_logic("PDK_OR2", false, false, false);
    check_logic("PDK_OR2", true, false, true);
    
    check_logic("PDK_XOR2", false, false, false);
    check_logic("PDK_XOR2", true, false, true);
    check_logic("PDK_XOR2", true, true, false);
    
    check_logic("PDK_XNOR2", false, false, true);
    check_logic("PDK_XNOR2", true, true, true);
    check_logic("PDK_XNOR2", true, false, false);
}

void test_mux() {
    std::cout << "Testing Multiplexers...\n";
    auto check_mux = [](const std::string& type, bool a, bool b, bool s, bool expected) {
        TensorNetlist nl;
        auto cell = StandardCells::StandardCellLibrary::instance().get(type);
        if (!cell) { std::cout << "Error: " << type << " not found\n"; return; }
        nl.blockDefinitions[type] = cell->internalBlock;
        
        // Pin Map: A=1, B=2, S=3, Y=4, VDD=5, VSS=0
        nl.addInstance(type, {0, 1, 2, 3, 4, 5});
        nl.addVoltageSource(5, 0, 1.8);
        nl.addVoltageSource(1, 0, a ? 1.8 : 0.0); // A
        nl.addVoltageSource(2, 0, b ? 1.8 : 0.0); // B
        nl.addVoltageSource(3, 0, s ? 1.8 : 0.0); // S
        nl.addResistor(4, 0, 1e6);
        
        CircuitSim sim;
        auto step = sim.solveDC(nl);
        
        double vy = step.nodeVoltages[4];
        bool result = (vy > 0.9);
        
        std::cout << "  " << type << "(A=" << a << ",B=" << b << ",S=" << s << ") -> " << vy << "V (" << result << ")\n";
        verify_pass(result == expected, type + " Truth Check");
    };
    
    check_mux("PDK_MUX2", true, false, false, true);  // S=0 -> Y=A=1
    check_mux("PDK_MUX2", true, false, true, false); // S=1 -> Y=B=0
    check_mux("PDK_MUX2", false, true, true, true);  // S=1 -> Y=B=1
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  COMPONENT ACCURACY TEST SUITE\n";
    std::cout << "========================================\n";
    
    test_basic_passives();
    test_diode();
    test_jfet();
    test_zener();
    test_logic_gates();
    test_mux();
    
    std::cout << "\nAll Checks Completed.\n";
    return 0;
}
