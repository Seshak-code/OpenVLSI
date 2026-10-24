#include <cstdint>
#include <iostream>
#include <cassert>
#include "acutesim_engine/netlist/circuit.h"
#include "acutesim_engine/netlist/netlist_diff.h"

void check(bool condition, const std::string& message) {
    if (condition) {
        std::cout << "[PASS] " << message << std::endl;
    } else {
        std::cout << "[FAIL] " << message << std::endl;
        exit(1);
    }
}

int main() {
    std::cout << "Starting Symbolic Diff Tests..." << std::endl;

    // Test 1: Identical Circuits
    {
        TensorBlock b1, b2;
        b1.name = "TestBlock"; b2.name = "TestBlock";
        
        b1.addResistor("R1", 1, 2, 1000.0);
        b2.addResistor("R1", 1, 2, 1000.0);
        
        b1.addCapacitor("C1", 2, 0, 1e-6);
        b2.addCapacitor("C1", 2, 0, 1e-6);
        
        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(report.isSemanticallyIdentical, "Identical circuits should match");
        check(report.changes.empty(), "No changes expected");
    }

    // Test 2: Parameter Change (Modified)
    {
        TensorBlock b1, b2;
        b1.addResistor("R1", 1, 2, 1000.0);
        b2.addResistor("R1", 1, 2, 2000.0); // Changed Value
        
        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(!report.isSemanticallyIdentical, "Modified circuit should not match");
        check(report.changes.size() == 1, "Expected 1 change");
        check(report.changes[0].changeType == DiffChangeType::MODIFIED_PARAMETER, "Expected MODIFIED_PARAMETER");
        check(report.changes[0].name == "R1", "Change should be on R1");
        check(report.changes[0].impactMask == (uint32_t)SimulationImpact::JACOBIAN_UPDATE, "Impact should be JACOBIAN_UPDATE");
    }

    // Test 3: Connectivity Change (Rewire)
    {
        TensorBlock b1, b2;
        b1.addResistor("R1", 1, 2, 1000.0);
        b2.addResistor("R1", 1, 3, 1000.0); // Changed Node 2 -> 3
        
        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(!report.isSemanticallyIdentical, "Rewired circuit should not match");
        check(report.changes[0].changeType == DiffChangeType::CONNECTIVITY_CHANGED, "Expected CONNECTIVITY_CHANGED");
        check(report.changes[0].impactMask & (uint32_t)SimulationImpact::MATRIX_REBUILD, "Impact should include MATRIX_REBUILD");
        // Note: 1-2 and 1-3 are isomorphic relative to ground 0 (both floating), so Hash is same.
        check(!report.isTopologyChanged, "Isomorphic rewiring should NOT change topology hash");
    }

    // Test 4: Added/Removed
    {
        TensorBlock b1, b2;
        b1.addResistor("R1", 1, 2, 1000.0);
        
        b2.addResistor("R1", 1, 2, 1000.0);
        b2.addCapacitor("C1", 2, 0, 1e-9); // Added
        
        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(report.changes.size() == 1, "Expected 1 change (Added)");
        check(report.changes[0].changeType == DiffChangeType::ADDED, "Expected ADDED");
        check(report.changes[0].impactMask & (uint32_t)SimulationImpact::STATE_RESET, "Impact should include STATE_RESET");
    }

    // Test 5: Deterministic Sort Order
    {
        TensorBlock b1, b2;
        // Changes: Add C2, Add C1. 
        // Insertion order: C2 then C1.
        // Sorted order should be C1, C2.
        
        b2.addCapacitor("C2", 1, 0, 1e-6);
        b2.addCapacitor("C1", 1, 0, 1e-6);
        
        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(report.changes.size() == 2, "Expected 2 changes");
        check(report.changes[0].name == "C1", "List should be sorted by name (C1 first)");
        check(report.changes[1].name == "C2", "List should be sorted by name (C2 second)");
    }

    // Test 6: True Topology Change (Series vs Parallel)
    {
        TensorBlock b1, b2;
        // b1: Parallel R1, R2 between 1 and 0.
        b1.addResistor("R1", 1, 0, 1000.0);
        b1.addResistor("R2", 1, 0, 1000.0);
        
        // b2: Series R1, R2 (1-2, 2-0).
        b2.addResistor("R1", 1, 2, 1000.0);
        b2.addResistor("R2", 2, 0, 1000.0);
        
        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(report.isTopologyChanged, "Series vs Parallel must change topology hash");
        check(!report.isSemanticallyIdentical, "Different circuits");
    }

    // Test 7: MOSFET Parameter Change (W/L sizing)
    {
        TensorBlock b1, b2;
        b1.addMosfet("M1", 1, 2, 0, 0, "nmos_generic", 2e-6, 0.18e-6);
        b2.addMosfet("M1", 1, 2, 0, 0, "nmos_generic", 4e-6, 0.18e-6); // W changed

        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(!report.isSemanticallyIdentical, "MOSFET W change should not be identical");
        check(report.changes.size() == 1, "Expected 1 MOSFET change");
        check(report.changes[0].changeType == DiffChangeType::MODIFIED_PARAMETER, "Expected MODIFIED_PARAMETER for MOSFET W");
        check(report.changes[0].name == "M1", "Change should be on M1");
    }

    // Test 8: VoltageSource Value Change (Index-based diff)
    {
        TensorBlock b1, b2;
        b1.addVoltageSource(1, 0, 5.0);
        b2.addVoltageSource(1, 0, 3.3); // Value changed

        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(!report.isSemanticallyIdentical, "VoltageSource value change should not be identical");
        check(report.changes.size() == 1, "Expected 1 VoltageSource change");
        check(report.changes[0].changeType == DiffChangeType::MODIFIED_PARAMETER, "Expected MODIFIED_PARAMETER for V");
    }

    // Test 9: Inductor Inductance Change
    {
        TensorBlock b1, b2;
        b1.addInductor("L1", 1, 2, 1e-3);
        b2.addInductor("L1", 1, 2, 2.2e-3); // Inductance changed

        DiffReport report = SemanticNetlistDiffer::diff(b1, b2);
        check(!report.isSemanticallyIdentical, "Inductor value change should not be identical");
        check(report.changes.size() == 1, "Expected 1 Inductor change");
        check(report.changes[0].changeType == DiffChangeType::MODIFIED_PARAMETER, "Expected MODIFIED_PARAMETER for L");
        check(report.changes[0].name == "L1", "Change should be on L1");
    }

    
    std::cout << "All Semantic Diff Tests Passed." << std::endl;
    return 0;
}
