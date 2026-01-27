#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include "../physics/circuitsim.h"
#include "../components/circuit.h"

int main() {
    std::cout << "=== DEPENDENT SOURCE SIMULATION TEST ===\n\n";

    // 1. VCVS Test
    // V1 (Node 1) = 5V
    // E1 Output (Node 2) = 2 * V(1)
    // R1 (Node 2 to 0) = 1k
    {
        std::cout << "  [VCVS] ";
        TensorNetlist netlist;
        netlist.numGlobalNodes = 2;
        netlist.addVoltageSource(1, 0, 5.0);
        netlist.addVCVS(2, 0, 1, 0, 2.0); // Out: 2, 0 | Ctrl: 1, 0 | Gain: 2.0
        
        CircuitSim sim;
        auto step = sim.solveDC(netlist);
        
        assert(step.stats.converged);
        assert(std::abs(step.nodeVoltages[0] - 5.0) < 1e-3); // V(1) = 5V
        assert(std::abs(step.nodeVoltages[1] - 10.0) < 1e-3); // V(2) = 10V
        std::cout << "PASS\n";
    }

    // 2. VCCS Test
    // V1 (Node 1) = 2V
    // G1 Output (Node 2) = 0.5 * V(1) [S] -> Iout = 1A
    // R1 (Node 2 to 0) = 10 Ohms -> V(2) = 10V
    {
        std::cout << "  [VCCS] ";
        TensorNetlist netlist;
        netlist.numGlobalNodes = 2;
        netlist.addVoltageSource(1, 0, 2.0);
        netlist.addVCCS(2, 0, 1, 0, 0.5); // Out: 2, 0 | Ctrl: 1, 0 | GM: 0.5
        netlist.addResistor(2, 0, 10.0);
        
        CircuitSim sim;
        auto step = sim.solveDC(netlist);
        
        assert(step.stats.converged);
        assert(std::abs(step.nodeVoltages[1] - (-10.0)) < 1e-2); // V(2) = -10V
        std::cout << "PASS\n";
    }

    // 3. CCVS Test
    // V1 (Node 1) = 1V
    // R_SENSE (Node 1 to 2) = 1.0 Ohms -> I_branch = 0.5A (with R_ext=1.0)
    // Actually, CCVS has its own internal 1.0 Ohm resistor at ctrlPos-ctrlNeg.
    // If we connect it between Node 1 and Node 2, it is in parallel with the 1.0 Ohm resistor.
    // I'll just connect it directly to Node 1 and GND to make it simple.
    {
        std::cout << "  [CCVS] ";
        TensorNetlist netlist;
        netlist.numGlobalNodes = 2;
        netlist.addVoltageSource(1, 0, 1.0);
        netlist.addCCVS(2, 0, 1, 0, 100.0); // Out: 2, 0 | Sense: 1 -> 0 | RM: 100.0
        
        CircuitSim sim;
        auto step = sim.solveDC(netlist);
        
        assert(step.stats.converged);
        // V(1) = 1V. Internal R_sense = 1.0. So I_sense = 1A.
        // V_out = rm * I_sense = 100 * 1 = 100V.
        // Polarities: I_sense flows 1->GND. V_out current flows 2->GND.
        // KCL at 2: G_out*V2 - (G_out*gain)*V1 = 0 -> V2 = gain*V1.
        // gain = rm / r_sense = 100 / 1.0 = 100.
        assert(std::abs(step.nodeVoltages[0] - 1.0) < 1e-3);
        assert(std::abs(step.nodeVoltages[1] - 100.0) < 1e-1);
        std::cout << "PASS\n";
    }

    // 4. CCCS Test
    // V1 (Node 1) = 1V
    // F1 Sense: Node 1 to GND (I_sense = 1V / 1.0 Ohm = 1A)
    // F1 Output: Node 2 to GND (Gain=10) -> Iout = 10A
    // R_LOAD (Node 2 to 0) = 1 Ohm -> Vout = -10V
    {
        std::cout << "  [CCCS] ";
        TensorNetlist netlist;
        netlist.numGlobalNodes = 2;
        netlist.addVoltageSource(1, 0, 1.0);
        netlist.addCCCS(2, 0, 1, 0, 10.0); // Out: 2, 0 | Sense: 1 -> 0 | Alpha: 10.0
        netlist.addResistor(2, 0, 1.0);
        
        CircuitSim sim;
        auto step = sim.solveDC(netlist);
        
        assert(step.stats.converged);
        // V(1) = 1.0. I_sense = 1.0. I_out = 10.0. V2 = -10.0.
        assert(std::abs(step.nodeVoltages[1] - (-10.0)) < 1e-1);
        std::cout << "PASS\n";
    }

    std::cout << "\nALL DEPENDENT SOURCE TESTS PASSED!\n";
    return 0;
}
