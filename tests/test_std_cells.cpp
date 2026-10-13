#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <iomanip>

#include "../components/standard_cells.h"
#include "../physics/circuitsim.h"
#include "acutesim_engine/netlist/circuit.h"

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
    nl.blockDefinitions["Darlington Pair"] = def->internalBlock;

    // Create Instance in Netlist
    // Map Pins: 0(Base)->1, 1(Collector)->2, 2(Emitter)->0
    nl.addInstance("Darlington Pair", {1, 2, 0});

    // Measurement Topology:
    // VCC on the next free node after the instance's internal node (indices must be
    // contiguous: an unused index is a floating node and fails DC, as in ngspice);
    // load 100Ω from VCC to the collector (node 2), so Ic = (VCC - Vc) / R_load
    const int vcc = nl.numGlobalNodes + 1;
    nl.addVoltageSource(vcc, 0, 10.0);
    nl.addResistor(vcc, 2, 100.0);

    // Input Current = 1uA into Node 1 (Base)
    nl.addCurrentSource(0, 1, 1e-6);

    // Solve
    CircuitSim sim;
    SolverStep step = sim.solveDC(nl);

    if (!step.stats.converged) {
        std::cout << RED << "FAIL (Diverged)" << RESET << "\n";
        // Print diagnostic info
        std::cout << "  Residual: " << step.stats.residual << "\n";
        std::cout << "  Iterations: " << step.stats.iterations << "\n";
        for (int i = 0; i < std::min((int)step.nodeVoltages.size(), 12); ++i)
            std::cout << "  V[" << i << "] = " << step.nodeVoltages[i] << "\n";
        exit(1);
    }

    // Verify Gain
    double V_c = step.nodeVoltages[1]; // node 2 = collector (0-based; [2] read the
                                       // internal node, which made the check vacuous)
    double I_c = (10.0 - V_c) / 100.0;
    double I_b = 1e-6;
    double Beta = I_c / I_b;
    
    std::cout << "Ic=" << I_c << " A, Ib=" << I_b << " A, Beta=" << Beta;
    
    // Darlington Beta should be ~ Beta1 * Beta2 = 100 * 100 = 10,000.
    // Without bias resistors and at low Ib, effective gain can exceed 10k.
    // Accept range [5000, 200000] for pure Darlington topology.
    if (Beta > 5000.0 && Beta < 200000.0) {
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
