#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include "physics/circuitsim.h"
#include "components/standard_cells.h"
#include "compute/tensors/physics_tensors.h"

using namespace StandardCells;

bool compare(const std::vector<double>& v1, const std::vector<double>& v2, double tol = 1e-9) {
    if (v1.size() != v2.size()) return false;
    for (size_t i = 0; i < v1.size(); ++i) {
        if (std::abs(v1[i] - v2[i]) > tol) {
            std::cout << std::scientific << std::setprecision(15);
            std::cout << "Mismatch at node " << i << ": " << v1[i] << " vs " << v2[i] 
                      << " (delta=" << std::abs(v1[i] - v2[i]) << ")\n";
            return false;
        }
    }
    return true;
}

int main() {
    std::cout << "Testing Legacy AoS vs Tensor SoA Scalar Consistency...\n";

    TensorNetlist nl;
    nl.environment.global_voltage_scale = 1.0;
    
    // Create Inverter
    auto def = createCMOSInverter();
    nl.blockDefinitions["INV"] = def.internalBlock;
    nl.addInstance("INV", {1, 2, 3, 0}); // VIN, VOUT, VDD, GND
    
    nl.addVoltageSource(3, 0, 1.8); // VDD
    nl.addVoltageSource(1, 0, 0.9); // IN (Bias at mid-point)

    // 1. Solve with Legacy AoS
    CircuitSim sim;
    sim.execMode = CircuitSim::ExecutionMode::LEGACY_AOS;
    SolverStep stepAoS = sim.solveDC(nl);
    
    std::cout << "Legacy AoS Node Voltages:\n";
    for (size_t i = 0; i < stepAoS.nodeVoltages.size(); ++i) {
        std::cout << "  Node " << i << ": " << stepAoS.nodeVoltages[i] << "V\n";
    }

    // 2. Solve with Tensor SoA Scalar
    sim.execMode = CircuitSim::ExecutionMode::TENSOR_SOA_SCALAR;
    SolverStep stepSoA = sim.solveDC(nl);
    
    std::cout << "Tensor SoA Node Voltages:\n";
    for (size_t i = 0; i < stepSoA.nodeVoltages.size(); ++i) {
        std::cout << "  Node " << i << ": " << stepSoA.nodeVoltages[i] << "V\n";
    }

    if (compare(stepAoS.nodeVoltages, stepSoA.nodeVoltages)) {
        std::cout << "[PASS] Legacy and SoA modes are consistent.\n";
        return 0;
    } else {
        std::cout << "[FAIL] Legacy and SoA modes diverged!\n";
        return 1;
    }
}
