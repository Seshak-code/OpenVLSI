#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include "physics/circuitsim.h"
#include "components/standard_cells.h"

// Check namespaces if needed. Assuming global for now based on header usage.
// If headers use namespaces, we add them here.
using namespace StandardCells; 

// Helper to verify conditions
#define ASSERT_TRUE(cond, msg) \
    if(!(cond)) { std::cout << "[FAIL] " << msg << "\n"; return 1; } \
    else { std::cout << "[PASS] " << msg << "\n"; }

int main() {
    std::cout << "========================================\n";
    std::cout << "  TRANSIENT SIMULATION TEST SUITE\n";
    std::cout << "========================================\n";

    // 1. D-Flip-Flop Verification
    {
        std::cout << "Testing PDK_DFF (Master-Slave)...\n";
        TensorNetlist nl;
        nl.environment.global_voltage_scale = 1.0;
        
        // Define DFF
        auto def = createPDK_DFF();
        nl.blockDefinitions["PDK_DFF"] = def.internalBlock;
        
        // Instantiate
        // Pin Map: D=1, CLK=2, Q=3, VDD=4, VSS=0
        // Local 0 (VSS) -> Global 0 (GND)
        // Local 1 (D) -> Global 1
        // Local 2 (CLK) -> Global 2
        // Local 3 (Q) -> Global 3
        // Local 4 (VDD) -> Global 4
        nl.addInstance("PDK_DFF", {0, 1, 2, 3, 4});
        
        // Power
        nl.addVoltageSource(4, 0, 1.8);
        
        // Simulation Loop
        CircuitSim sim;
        sim.setIntegrationMethod(IntegrationType::TRAPEZOIDAL);
        
        double t = 0.0;
        double dt = 10e-12; // 10ps step
        double t_end = 40e-9; // 40ns
        
        // Signal Pattern
        // Period 10ns. Edges at 5, 15, 25.
        // 0-8ns: D=0. (Latch 0 at 5).
        // 8-18ns: D=1. (Latch 1 at 15).
        // 18-30ns: D=0. (Latch 0 at 25).
        auto getD = [&](double time) {
            if (time < 8e-9) return 0.0;
            if (time < 18e-9) return 1.8;
            return 0.0;
        };
        
        auto getClk = [&](double time) {
            // Period 10ns. 0-5 L, 5-10 H?
            // Phase shift to rise at 5.
            double phase = fmod(time, 10e-9);
            return (phase >= 5e-9) ? 1.8 : 0.0;
        };

        std::cout << "Time(ns) | CLK | D | Q\n";
        
        bool checked_reset = false;
        bool checked_rise = false;
        bool checked_fall = false;
        
        while (t < t_end) {
            // Apply Stimulus
            double v_d = getD(t);
            double v_clk = getClk(t);
            
            if (t == 0.0) {
                nl.addVoltageSource(1, 0, v_d); // Src 1: D
                nl.addVoltageSource(2, 0, v_clk); // Src 2: CLK
            } else {
                for(auto& vs : nl.globalBlock.voltageSources) {
                    if (vs.nodePositive == 1) vs.voltage_V = v_d;
                    if (vs.nodePositive == 2) vs.voltage_V = v_clk;
                }
            }
            
            SolverStep step = sim.stepTransient(nl, dt, t);
            double v_q = step.nodeVoltages[3];
            
            if (fmod(t, 1e-9) < dt) { // Print every 1ns
                 double v6 = step.nodeVoltages[6];
                 double v7 = step.nodeVoltages[7];
                 double v8 = step.nodeVoltages[8];
                 std::cout << std::fixed << std::setprecision(1) 
                           << (t*1e9) << " | " 
                           << (v_clk > 0.9) << " | " 
                           << (v_d > 0.9) << " | " 
                           << (v_q > 0.9) << " (" << v_q << "V) "
                           << "Int: " << v6 << " " << v7 << " " << v8 << "\n";
            }
             
            // t=7ns (After Rise 5): Latched 0. Q should be 0.
            if (t > 7e-9 && t < 7.1e-9 && !checked_reset) {
                if (v_q > 0.5) { std::cout << "FAIL: Q did not reset to 0!\n"; return 1; }
                checked_reset = true;
            }
            
            // t=17ns (After Rise 15): Latched 1. Q should be 1.
            if (t > 17e-9 && t < 17.1e-9 && !checked_rise) {
                 if (v_q < 1.0) { std::cout << "FAIL: Q did not rise after clock edge!\n"; return 1; }
                 checked_rise = true;
            }
            
            // t=27ns (After Rise 25): Latched 0. Q should be 0.
            if (t > 27e-9 && t < 27.1e-9 && !checked_fall) {
                 if (v_q > 0.5) { std::cout << "FAIL: Q did not fall after clock edge!\n"; return 1; }
                 checked_fall = true;
            }
            
            t += dt;
        }
        
        if (!checked_reset || !checked_rise || !checked_fall) {
            std::cout << "FAIL: Did not reach check points!\n";
            return 1;
        }
    }
    
    std::cout << "All Transient Tests Completed.\n";
    return 0;
}
