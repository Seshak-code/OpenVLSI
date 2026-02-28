#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include "physics/circuitsim.h"
#include "components/standard_cells.h"
#include "acutesim_engine/tensors/physics_tensors.h"

using namespace StandardCells;

struct ModeResult {
    CircuitSim::ExecutionMode mode;
    std::string name;
    std::vector<double> voltages;
    double residual;
    int iterations;
};

bool compare(const std::vector<double>& v1, const std::vector<double>& v2, double tol = 1e-9) {
    if (v1.size() != v2.size()) return false;
    for (size_t i = 0; i < v1.size(); ++i) {
        if (std::abs(v1[i] - v2[i]) > tol) {
            std::cout << std::scientific << std::setprecision(15);
            std::cout << "    Mismatch at node " << i << ": " << v1[i] << " vs " << v2[i] 
                      << " (delta=" << std::abs(v1[i] - v2[i]) << ")\n";
            return false;
        }
    }
    return true;
}

void runTest(const std::string& testName, const TensorNetlist& nl) {
    std::cout << "\n======================================================================\n";
    std::cout << "TEST CASE: " << testName << "\n";
    std::cout << "======================================================================\n";

    std::vector<ModeResult> results;
    std::vector<CircuitSim::ExecutionMode> modes = {
        CircuitSim::ExecutionMode::LEGACY_AOS,
        CircuitSim::ExecutionMode::TENSOR_SOA_SCALAR
        // TENSOR_SOA_SIMD is currently identical to SCALAR in the SoA pathway
    };
    std::vector<std::string> modeNames = {"Legacy AoS", "Tensor SoA Scalar"};

    CircuitSim sim;
    for (size_t i = 0; i < modes.size(); ++i) {
        sim.execMode = modes[i];
        SolverStep step = sim.solveDC(const_cast<TensorNetlist&>(nl));
        results.push_back({modes[i], modeNames[i], step.nodeVoltages, step.stats.residual, step.stats.iterations});
        
        std::cout << "[" << modeNames[i] << "] Converged in " << step.stats.iterations 
                  << " iters, Res=" << std::scientific << std::setprecision(6) << step.stats.residual << "\n";
    }

    // Compare all to Legacy
    bool allPassed = true;
    for (size_t i = 1; i < results.size(); ++i) {
        std::cout << "  Comparing " << results[i].name << " to Legacy... ";
        if (compare(results[0].voltages, results[i].voltages)) {
            std::cout << "[PASS]\n";
        } else {
            std::cout << "[FAIL]\n";
            allPassed = false;
        }
    }

    if (allPassed) {
        std::cout << "TEST " << testName << " PASSED.\n";
    } else {
        std::cout << "TEST " << testName << " FAILED.\n";
        exit(1);
    }
}

int main() {
    std::cout << "Starting Mode Ladder Regression Suite...\n";

    // Case 1: CMOS Inverter
    {
        TensorNetlist nl;
        nl.environment.global_voltage_scale = 1.0;
        auto def = createCMOSInverter();
        nl.blockDefinitions["INV"] = def.internalBlock;
        nl.addInstance("INV", {1, 2, 3}); // Node 1:VIN, 2:VOUT, 3:VDD
        nl.addVoltageSource(3, 0, 1.8); // VDD
        nl.addVoltageSource(1, 0, 0.9); // VIN @ 0.9V (Switching point)
        runTest("CMOS Inverter (Mid-Rail)", nl);
    }

    // Case 2: BJT Differential Pair
    {
        TensorNetlist nl;
        nl.environment.global_voltage_scale = 1.0;
        auto def = createDifferentialPair();
        nl.blockDefinitions["DIFF"] = def.internalBlock;
        // Nodes in DIFF: 1:IN+, 2:IN-, 3:OUT-, 4:OUT+, 5:Tail, 6:VCC
        // Mapping to Global: 1:IN+, 2:IN-, 3:OUT+, 4:OUT-, 5:VCC, 6:Tail
        // Vector: {1, 2, 4, 3, 6, 5} -> Node 1->1, 2->2, 3->4, 4->3, 5->6, 6->5
        nl.addInstance("DIFF", {1, 2, 4, 3, 6, 5}); 
        nl.addVoltageSource(5, 0, 5.0); // VCC (Global 5)
        nl.addVoltageSource(1, 0, 0.1); // IN+ = 100mV
        nl.addVoltageSource(2, 0, -0.1); // IN- = -100mV
        runTest("BJT Differential Amplifier", nl);
    }
    // Case 3: Darlington Pair (High Gain)
    {
        TensorNetlist nl;
        nl.environment.global_voltage_scale = 1.0;
        auto def = createGenericDarlington();
        nl.blockDefinitions["DARL"] = def.internalBlock;
        // Nodes in DARL: 1:B, 2:C, 3:E, 4:Internal
        // Mapping: {1, 2, 0, 4} -> Node 1->1, 2->2, 3->0(GND), 4->4
        nl.addInstance("DARL", {1, 2, 0, 4}); 
        nl.addVoltageSource(2, 0, 12.0); // V_Collector (Global 2)
        nl.addVoltageSource(1, 0, 1.4);  // V_Base (2 * 0.7V)
        runTest("Darlington Pair (High Gain)", nl);
    }

    std::cout << "\nAll Mode Ladder tests PASSED successfully.\n";
    return 0;
}
