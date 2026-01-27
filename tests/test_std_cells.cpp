#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <iomanip>

#include "../components/standard_cells.h"
#include "../physics/circuitsim.h"
#include "../components/circuit.h"

// ANSI Colors
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

/**
 * test_std_cells.cpp
 * 
 * Verifies that components in the StandardCellLibrary are correctly
 * constructed and can be instantiated in a simulation.
 * 
 * Focus: Generic Darlington Pair
 */

using namespace StandardCells;

void verify_component_topology(const std::string& name) {
    const SubCircuitDefinition* def = StandardCellLibrary::instance().get(name);
    if (!def) {
        std::cout << "FAIL: " << name << " not found in library.\n";
        exit(1);
    }
    
    // Rigorous Pin Integrity Check
    // 1. Pin names match count
    if (def->pinCount != (int)def->pinNames.size()) {
        std::cout << "FAIL: " << name << " pin name mismatch (" 
                  << def->pinCount << " spec vs " << def->pinNames.size() << " actual)\n";
        exit(1);
    }
    
    // 2. Mapping covers all pins
    for (int i = 0; i < def->pinCount; ++i) {
        if (def->pinToInternalNode.find(i) == def->pinToInternalNode.end()) {
            std::cout << "FAIL: " << name << " missing mapping for pin " << i << " (" << def->pinNames[i] << ")\n";
            exit(1);
        }
    }
    
    // 3. Mapping within bounds of internal nodes
    for (auto const& [pin, node] : def->pinToInternalNode) {
        if (node < 0 || node > def->internalBlock.numInternalNodes) {
            std::cout << "FAIL: " << name << " pin " << pin << " maps to invalid node " << node << "\n";
            exit(1);
        }
    }
    
    // Instantiate in Netlist to verify node expansion
    TensorNetlist nl;
    nl.blockDefinitions[name] = def->internalBlock;
    
    // Build dummy map
    std::vector<int> dummyMap;
    for(int i=0; i<def->pinCount; ++i) dummyMap.push_back(i+1); // G1..GN
    
    try {
        nl.addInstance(name, dummyMap); 
        std::cout << "[OK] " << std::setw(30) << std::left << name << " | Pins: " << def->pinCount 
                  << " | Internal Nodes: " << def->internalBlock.numInternalNodes << "\n";
    } catch (const std::exception& e) {
        std::cout << "FAIL: " << name << " instantiation error: " << e.what() << "\n";
        exit(1);
    }
}

void test_generic_darlington() {
    std::cout << "[TEST] Standard Library Topology & Pin Verification...\n";
    
    std::vector<std::string> components = {
        "Darlington Pair",
        "Resistive Divider (10k:10k)",
        "Half-Wave Rectifier",
        "CMOS Inverter",
        "BJT Differential Pair",
        "RC Low-Pass Filter",
        "BiCMOS Buffer",
        
        // PDK Templates
        "PDK_NFET",
        "PDK_PFET",
        "PDK_Resistor_Poly",
        "PDK_Capacitor_MIM"
    };

    for (const auto& name : components) {
         verify_component_topology(name);
    }
    
    std::cout << "\n[TEST] Simulating Darlington Gain... ";
    
    // 1. Retrieve Definition
    const SubCircuitDefinition* def = StandardCellLibrary::instance().get("Darlington Pair");
    if (!def) {
        std::cout << RED << "FAIL (Not Found)" << RESET << "\n";
        exit(1);
    }
    
    // 2. Verify Topology
    // Expect 3 pins (B, C, E)
    if (def->pinCount != 3) {
         std::cout << RED << "FAIL (Pin Count Mismatch: " << def->pinCount << " != 3)" << RESET << "\n";
         exit(1);
    }
    
    // Expect internal block has 2 BJTs + 2 Resistors
    if (def->internalBlock.bjts.size() != 2) {
        std::cout << RED << "FAIL (BJT Count Mismatch)" << RESET << "\n";
        exit(1);
    }
    
    std::cout << GREEN << "OK (Topology)" << RESET << "\n";
    
    // 3. Verify Physics (Simulation)
    std::cout << "[TEST] Simulating Darlington Gain... ";
    
    TensorNetlist nl;
    
    // Setup Library
    // We need to populate `nl.blockDefinitions` with our library cell
    nl.blockDefinitions["Darlington Pair"] = def->internalBlock; // Copy block definition
    
    // Create Instance in Netlist
    // Use helper method to adding instance and map nodes simultaneously
    // Map Pins: 0(Base)->1, 1(Collector)->2, 2(Emitter)->0
    nl.addInstance("Darlington Pair", {1, 2, 0});
    
    // Support infrastructure
    
    // Support infrastructure
    // VCC = 10V at Node 2 (Initially direct)
    nl.addVoltageSource(2, 0, 10.0);
    
    // Input Current = 500uA into Node 1
    // Connect Source Positive to Node 1. Source Negative to GND.
    nl.addCurrentSource(1, 0, 1e-6);
    

    
    // Solve
    CircuitSim sim;
    SolverStep step = sim.solveDC(nl);
    
    if (!step.stats.converged) {
        std::cout << RED << "FAIL (Diverged)" << RESET << "\n";
        exit(1);
    }
    
    // Verify Gain
    // Ib = 1uA
    // Ic = Current from VCC (Node 2).
    
    // Let's redo topology slightly for measurement.
    // VCC(3) = 10V.
    // Resistor(3, 2, 1.0)
    // Q.C = 2.
    // Q.B = 1.
    // Q.E = 0.
    // I_base = 1uA source(1, 0).
    // Access globalBlock to clear voltage sources
    nl.globalBlock.voltageSources.clear(); 
    nl.addVoltageSource(10, 0, 10.0);
    nl.addResistor(10, 2, 100.0);
    
    // Re-solve
    step = sim.solveDC(nl);
    
    double V_c = step.nodeVoltages[2]; // Node 3 is 10V, Node 2 is Collector.
    double I_c = (10.0 - V_c) / 100.0;
    double I_b = 1e-6;
    double Beta = I_c / I_b;
    
    std::cout << "Ic=" << I_c << " A, Ib=" << I_b << " A, Beta=" << Beta;
    
    // Darlington Beta should be ~ Beta1 * Beta2 = 100 * 100 = 10,000.
    // With bias resistors diverting some current, it might be slightly lower (e.g. 8000).
    if (Beta > 2000.0 && Beta < 15000.0) {
        std::cout << GREEN << " PASS (High Gain Verified)" << RESET << "\n";
    } else {
        std::cout << RED << " FAIL (Gain Out of Range)" << RESET << "\n";
        exit(1);
    }
}

int main() {
    try {
        test_generic_darlington();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
